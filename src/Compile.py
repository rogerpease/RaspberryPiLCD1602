#!/usr/bin/env python3 

import os

os.environ['PATH'] = '/home/rpease/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin:'+os.environ['PATH']
os.environ['TOOLCHAIN'] = "/home/rpease/tools/arm-bcm2700/gcc-linaro-arm-linux-gnueabihf-raspbian-x64"
os.environ["CROSS_COMPILE"] = "arm-linux-gnueabihf-"
os.environ['ARCH']='arm'

  
os.system("make ARCH=arm CROSS_COMPILE="+os.environ["CROSS_COMPILE"] +" all ") 
