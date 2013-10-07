# Example gdb script for debugging remotely on Android device
target remote 192.168.1.16:5050
symbol-file ./etna_test
break main
cont
sharedlibrary

