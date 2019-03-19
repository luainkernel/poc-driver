#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Pedro Tammela <pctammela@gmail.com>");
MODULE_DESCRIPTION("sample driver for lunatik proof of concepts");

#define DEVICE_NAME "luadrv"
#define CLASS_NAME "lua"
#define LUA_MAX_MINORS  1

#define raise_err(msg) pr_warn("[lua] %s - %s\n", __func__, msg);

static DEFINE_MUTEX(mtx);

static lua_State *L;
static bool hasreturn = 0; /* does the lua state have anything for us? */
static dev_t dev;
static struct device *luadev;
static struct class *luaclass;
static struct cdev luacdev;

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
	int ret;

	ret = alloc_chrdev_region(&dev, 0, LUA_MAX_MINORS, "lua");
	if (ret) {
		raise_err("alloc_chrdev_region failed");
		goto error;
	}

	cdev_init(&luacdev, &fops);
	ret = cdev_add(&luacdev, dev, LUA_MAX_MINORS);
	if (ret) {
		raise_err("cdev_add failed");
		goto error_free_region;
	}

	luaclass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(luaclass)) {
		raise_err("class_create failed");
		ret = PTR_ERR(luaclass);
		goto error_free_cdev;
	}

	luadev = device_create(luaclass, NULL, dev,
			NULL, "%s", DEVICE_NAME);
	if (IS_ERR(luadev)) {
		raise_err("device_create failed");
		ret = PTR_ERR(luadev);
		goto error_free_class;
	}

	L = luaL_newstate();
	if (L == NULL) {
		raise_err("no memory");
		ret = -ENOMEM;
		goto error_free_device;
	}
	luaL_openlibs(L);

	return 0;

error_free_device:
	device_destroy(luaclass, dev);
error_free_class:
	class_destroy(luaclass);
error_free_cdev:
	cdev_del(&luacdev);
error_free_region:
	unregister_chrdev_region(dev, LUA_MAX_MINORS);
error:
	return ret;
}

static void __exit luadrv_exit(void)
{
	lua_close(L);

	device_destroy(luaclass, dev);
	class_destroy(luaclass);
	cdev_del(&luacdev);
	unregister_chrdev_region(dev, LUA_MAX_MINORS);
}

static int dev_open(struct inode *i, struct file *f)
{
	return 0;
}

static ssize_t dev_read(struct file *f, char *buf, size_t len, loff_t *off)
{
	const char *msg = "Nothing yet.\n";
	int msglen;
	int err;
	mutex_lock(&mtx);
	if (hasreturn) {
		msg = lua_tostring(L, -1);
		hasreturn = false;
	}
	if ((err = copy_to_user(buf, msg, len)) < 0) {
		raise_err("copy to user failed");
		mutex_unlock(&mtx);
		return -ECANCELED;
	}
	mutex_unlock(&mtx);
	msglen = strlen(msg);
	return msglen < len ? msglen : len;
}

static int flushL(void)
{
	lua_close(L);
	L = luaL_newstate();
	if (L == NULL) {
		raise_err("flushL failed, giving up");
		mutex_unlock(&mtx);
		return 1;
	}
	luaL_openlibs(L);
	raise_err("lua state flushed");
	return 0;
}

static ssize_t dev_write(struct file *f, const char *buf, size_t len,
		loff_t* off)
{
	char *script = NULL;
	int idx = lua_gettop(L);
	int err;
	mutex_lock(&mtx);
	script = kmalloc(len, GFP_KERNEL);
	if (script == NULL) {
		raise_err("no memory");
		return -ENOMEM;
	}
	if ((err = copy_from_user(script, buf, len)) < 0) {
		raise_err("copy from user failed");
		mutex_unlock(&mtx);
		return -ECANCELED;
	}
	script[len - 1] = '\0';
	if (luaL_dostring(L, script)) {
		raise_err(lua_tostring(L, -1));
		if (flushL()) {
			return -ECANCELED;
		}
		mutex_unlock(&mtx);
		return -ECANCELED;
	}
	kfree(script);
	hasreturn = lua_gettop(L) > idx ? true : false;
	mutex_unlock(&mtx);
	return len;
}

static int dev_release(struct inode *i, struct file *f)
{
	mutex_lock(&mtx);
	hasreturn = false;
	mutex_unlock(&mtx);
	return 0;
}

module_init(luadrv_init);
module_exit(luadrv_exit);
