#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Pedro Tammela <pctammela@gmail.com>");
MODULE_AUTHOR("lunatik team (https://github.com/luainkernel)");
MODULE_DESCRIPTION("Basic kernel module that provides /dev/luadrv character device to load and execute arbitrary lua code");

#define DEVICE_NAME "luadrv"
#define CLASS_NAME "lua"
#define NSTATES 4
// currently supported only one device
#define LUA_MAX_MINORS  1

#define raise_err(msg) pr_warn("[lua] %s - %s\n", __func__, msg);
#define print_info(msg) pr_warn("[lua] %s\n", msg);

typedef struct device_data {
	dev_t dev;
	struct device *luadev;
	struct class *luaclass;
	struct cdev luacdev;
	struct mutex lock;
} device_data;

static device_data devs[LUA_MAX_MINORS];

typedef struct lua_exec {
	int id;
	lua_State *L;
	int stacktop;
	char *script;
	struct task_struct *kthread;
	struct mutex lock;
} lua_exec;

static lua_exec lua_states[NSTATES];

static int dev_open(struct inode*, struct file*);
static int dev_release(struct inode*, struct file*);
static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct file_operations fops =
{
	.open = dev_open,
	.read = dev_read,
	.write = dev_write,
	.release = dev_release
};

static int __init luadrv_init(void)
{
	int ret, i, j;
	device_data *dev = &devs[0];

	ret = alloc_chrdev_region(&dev->dev, 0, LUA_MAX_MINORS, "lua");
	if (ret) {
		raise_err("alloc_chrdev_region failed");
		goto error;
	}

	cdev_init(&dev->luacdev, &fops);
	ret = cdev_add(&dev->luacdev, dev->dev, LUA_MAX_MINORS);
	if (ret) {
		raise_err("cdev_add failed");
		goto error_free_region;
	}

	dev->luaclass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(dev->luaclass)) {
		raise_err("class_create failed");
		ret = PTR_ERR(dev->luaclass);
		goto error_free_cdev;
	}

	dev->luadev = device_create(dev->luaclass, NULL, dev->dev,
			NULL, "%s", DEVICE_NAME);
	if (IS_ERR(dev->luadev)) {
		raise_err("device_create failed");
		ret = PTR_ERR(dev->luadev);
		goto error_free_class;
	}

	mutex_init(&dev->lock);

	for (i = 0; i < NSTATES; i++) {
		lua_states[i].id = i;
		lua_states[i].L = luaL_newstate();

		if (lua_states[i].L == NULL) {
			raise_err("no memory");
			ret = -ENOMEM;

			for (j = 0; j < i; j++) {
				lua_close(lua_states[j].L);
			}

			goto error_free_device;
		}

		luaL_openlibs(lua_states[i].L);
		mutex_init(&lua_states[i].lock);
	}

	return 0;

error_free_device:
	device_destroy(dev->luaclass, dev->dev);
error_free_class:
	class_destroy(dev->luaclass);
error_free_cdev:
	cdev_del(&dev->luacdev);
error_free_region:
	unregister_chrdev_region(dev->dev, LUA_MAX_MINORS);
error:
	return ret;
}

static void __exit luadrv_exit(void)
{
	int i;
	device_data *dev = &devs[0];

	for (i = 0; i < NSTATES; i++) {
		lua_close(lua_states[i].L);
	}

	device_destroy(dev->luaclass, dev->dev);
	class_destroy(dev->luaclass);
	cdev_del(&dev->luacdev);
	unregister_chrdev_region(dev->dev, LUA_MAX_MINORS);
}

static int dev_open(struct inode *i, struct file *f)
{
	return 0;
}

static ssize_t dev_read(struct file *f, char *buf, size_t len, loff_t *off)
{
	return 0;
}

static lua_State* flush(lua_State *L)
{
	lua_close(L);
	L = luaL_newstate();
	if (L == NULL) {
		raise_err("flushL failed, giving up");
		return NULL;
	}
	luaL_openlibs(L);
	raise_err("lua state flushed");
	return L;
}

static int thread_fn(void *arg)
{
	int ret = 0;
	lua_exec *lua = arg;
	set_current_state(TASK_INTERRUPTIBLE);

	printk("[lua] running thread %d\n", lua->id);
	if (luaL_dostring(lua->L, lua->script)) {
		raise_err("script error, flushing the state\n");
		printk("%s\n", lua_tostring(lua->L, -1));
		lua->L = flush(lua->L);
		ret = -ECANCELED;
	} else if (lua_gettop(lua->L) > lua->stacktop) {
		printk("[lua] thread %d result: %s\n", lua->id, lua_tostring(lua->L, -1));
	}

	kfree(lua->script);
	mutex_unlock(&lua->lock);

	printk("[lua] thread %d finished\n", lua->id);
	return ret;
}

static ssize_t dev_write(struct file *f, const char *buf, size_t len,
		loff_t* off)
{
	device_data *dev = &devs[0];
	int ret, i;
	char *script = NULL;

	mutex_lock(&dev->lock);

	script = kmalloc(len + 1, GFP_KERNEL);
	if (script == NULL) {
		raise_err("no memory");
		ret = -ENOMEM;
		goto return_unlock;
	}

	if (copy_from_user(script, buf, len) < 0) {
		raise_err("copy from user failed");
		ret = -ECANCELED;
		goto return_free;
	}
	script[len] = '\0';

	for (i = 0; i < NSTATES; i++) {
		if (lua_states[i].L != NULL && mutex_trylock(&lua_states[i].lock)) {
			lua_states[i].stacktop = lua_gettop(lua_states[i].L);
			lua_states[i].script = script;
			lua_states[i].kthread = kthread_run(thread_fn, &lua_states[i], "lua kthread %d", lua_states[i].id);
			if(IS_ERR(lua_states[i].kthread)) {
				ret = PTR_ERR(lua_states[i].kthread);
				goto return_free;
			}

			ret = len;
			goto return_unlock;
		}
	}

	raise_err("all lua states are busy");
	ret = -EBUSY;

return_free:
	kfree(script);
return_unlock:
	mutex_unlock(&dev->lock);
	return ret;
}

static int dev_release(struct inode *i, struct file *f)
{
	return 0;
}

module_init(luadrv_init);
module_exit(luadrv_exit);
