The fifo file (under dev) is created automatically when installing
the module (see below).

The major number is assigned dynamically by register_chrdev, so we peek into
/proc/devices and find out what the kernel registered. (taken from LDD)

To load module: sudo make install
To unload module: sudo make uninstall
