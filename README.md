# Xv6-Operating-System
Added new control sequences such as 'control -p' which prints process information.
Upgraded scheduler to round robin.
Added commands such as 'date'.     
||||
vvvv

![1](https://user-images.githubusercontent.com/54726842/121793456-f7afa500-cbb3-11eb-8c62-f6e584f35b10.PNG)
![2](https://user-images.githubusercontent.com/54726842/121793457-f8483b80-cbb3-11eb-8747-5533e9591da2.PNG)
 

The Makefile provided with xv6 has several phony targets for running the system:

make qemu
Build everything and run xv6 with QEMU, with a VGA console in a new window and the serial console in the terminal where you typed this command. Close the VGA window or press Ctrl-C or Ctrl-A X to stop.
make qemu-nox
Run xv6 without the VGA console.
make qemu-gdb
Run xv6 with GDB port open. Refer to the GDB section.
make qemu-nox-gdb
Run xv6 with GDB port open, without the VGA console.
