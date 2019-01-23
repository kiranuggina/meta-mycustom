# Character device driver

After running `make` command, there will be one module:

* scull-char.ko

After loading the module, there will be a `/dev/scull_char` char device. There
is also a class created for this device, `/sys/class/scull_char_class/`. One can
actually print info on the device using `udevadm info` command:


```bash
# insmod scull-char.ko
# udevadm info /dev/scull_char
P: /devices/virtual/scull_char_class/scull_char
N: scull_char
E: DEVNAME=/dev/scull_char
E: DEVPATH=/devices/virtual/scull_char_class/scull_char
E: MAJOR=241
E: MINOR=0
E: SUBSYSTEM=scull_char_class


$ ls -l /sys/class/scull_char_class/
total 0
lrwxrwxrwx 1 root root 0 oct.  12 16:05 scull_char -> ../../devices/virtual/scull_char_class/scull_char
$ cat /sys/class/scull_char_class/scull_char/dev 
241:0
```

For testing purpose, one can use `cat` and `read` commands:

```bash
# cat /dev/scull_char 
# echo "blabla" > /dev/scull_char 
# rmmod scull-char.ko 

$ dmesg
[...]
[31444.392114] scull_char major number = 241
[31444.392217] scull char module loaded
[31452.575938] Someone tried to open me
[31452.575945] Nothing to read guy
[31452.575950] Someone closed me
[31483.210527] Someone tried to open me
[31483.210570] Can't accept any data guy
[31483.210578] Someone closed me
[31498.998185] scull char module Unloaded
```
