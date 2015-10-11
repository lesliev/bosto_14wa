This is a Linux driver for the 2nd generation Bosto Kingtee 14WA tablet, released in November 2013.

Using this driver requires some manual work but is definitely possible right now. We are
working to get the drivers to a point where using these tablets under Linux will be automatic
and convenient.

Multiple drivers
================

The 14WA tablet requires two different drivers, one for the pen and one for the keys.
The pen driver works quite well (with proper tracking and pressure support) but key support
is handled by the USBHID driver which treats the tablet like a keyboard. This means the keys
emulate numbers and letters. We are working to improve this.

Current Status
==============

* Pen pressure: working
* Pen tracking: working
* Keys: produce numbers like a numeric keypad
* Scroll wheels: produce 'a', 'b', 'c', 'd' like a keyboard

**Tested programs**

Tracking and pressure sensitivity working on:

* GIMP 2.8
* MyPaint 1.1.0
* Inkscape 0.48
* Synfig 0.64.1
* Krita 2.9 pre-alpha (3 August 2014)

In each program, you will need to find the "Input Devices" configuration and select the Bosto (change 'disabled' to 'screen') and you might need to map axis 3 to pressure (but that's usually the default). 

* Krita 2.7.2 has tracking but it's unknown right now whether pressure works.
* Krita 2.8.1 doesn't seem to work at all.
* Krita 2.9 pre-alpha (3 August 2014) works fine.

In MyPaint I have needed to select the tablet each time to get pressure support. This is how:
1. Help -> Debug -> GTK Input Device Dialog
2. Select the Bosto in the drop down list
3. Choose 'screen' for the mode
4. Make sure pressure is on axis 3

After that I've had to dismiss the dialog with 'esc'.

**Distributions**

The installation has been tested on Ubuntu 13.10 and Ubuntu 14.04. Please let us know your experiences on other distributions.

Installation
============

**Build and install the driver**

```bash
sudo apt-get install build-essential linux-headers-generic git     # install requirements
cd ~
git clone https://github.com/lesliev/bosto_14wa.git
cd bosto_14wa
make clean && make
sudo make install
```

Now if you plug in the tablet, the pen should work right away. If not, please post an issue and we'll try to improve the code.

Multiple screens
================

You will get best results if you run the 'displays' program and configure the tablet and main screen to mirror each other - but this means you get "lowest common denominator" resolution on both screens. Still, that's not always bad because lower resolution makes the tablet perform a lot better.

If your screen resolutions don't match or you want multiple screens, you can use 'xinput' to map the stylus to only the tablet screen. I've written a script to do that:

```bash
cd scripts
./inputtransform
```

This calculates and then uses xinput to apply a "coordinate transformation matrix".
Alternatively, xinput can do the same calculations and you don't need the script. This is how:

1. Look up the name of the monitor and the stylus/eraser devices by running `xrandr` and `xinput`.
2. Run xinput --map-to-output for each device:

```bash
xinput --map-to-output 'Bosto Kingtee 14WA stylus' HDMI1
xinput --map-to-output 'Bosto Kingtee 14WA eraser' HDMI1
```

The script gives you a menu, which is sometimes faster - and can reset the mapping like so:
`./inputtransform --reset`


Uninstalling
============

```bash
sudo make uninstall
```


TODO
====

1. Make the pen driver load automatically when the tablet is plugged in  <-- done (until we find a nicer solution)
2. Write another or configure USBHID driver to allow remapping of keys and scroll wheels
3. Try to get the driver updated in the kernel tree so no installation is required in future

Diagnostics
===========

After running modprobe, check if the module was loaded properly with dmesg.
"Bosto Kingtee 14WA" should appear in the listing.

lsmod should also contain `bosto_14wa` in its listing: lsmod | grep bosto

Debug ouput now pattern matched to entries in the /sys/kernel/debug/dynamic_debug/control file
For example to see each time the driver detects a PEN_IN event, echo the following:

echo -n 'format "PEN_IN" +p' > <debugfs>/control

(See https://www.kernel.org/doc/Documentation/dynamic-debug-howto.txt )

There's a script in 'scripts' called 'turn_on_debug' which does this for me. After you've run it you can run:
sudo tail -f /var/log/syslog

... to see the tablet's events being written to the system log file.

Feedback
========

The best place for feedback is the Bosto forum: http://forum.bosto.co/viewforum.php?f=93
You can also use the Github issue tracker for issues specific to the driver.

