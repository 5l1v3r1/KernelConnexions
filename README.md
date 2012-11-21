KernelConnexions
================

KernelConnexions will be a Kernel extension which user-space programs can utilize to connect to TCP sockets. Why not use the user-space libraries, you might ask? My primary motivation behind this is to essentially *mask* the PID of the task which is actually doing the TCP communication. This way, the user-space program can bypass programs such as LittleSnitch. In addition, this may be useful to bypass some network filters, although I have yet to do any tests for this.

Under Development
=================

Currently, this does not work. I don't know if it ever will.

License
=======

If you reuse this you will be smitten by whatever God or lackthereof you believe in.
