#!/bin/bash
make
sudo rmmod poc-driver
sudo rmmod lunatik
sudo insmod ./dependencies/lunatik/lunatik.ko
sudo insmod poc-driver.ko

