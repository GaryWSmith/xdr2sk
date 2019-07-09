# xdr2sk
A NMEA0183 XDR message to Signal K converter

This program converts NMEA0183 XDR messages to Signal K delta format. https://signalk.org/
It is C based and should compile on any Linux platform

It uses TCP sockets for both input and output. It is assumed that the NMEA XDR messages will originate from something like kplex.
The input side is therefore a TCP client.
The Signal K server supports a TCP connection to a TCP server, therefore the output side is a TCP server.

The address and port details are defined in a dictionary file.

XDR messages are used to provide data relating to transducers. They have a reputation for being ad hoc and are somewhat unsupported - hence this program. XDR messages are made up of one or more quadruples. The second field in the quadruple contains the data and the 4th field is the name field. https://en.wikipedia.org/wiki/NMEA_0183

Here are examples:

$IIXDR,C,28.7,C,ENV_OUTSIDE_T,P,101400,P,ENV_ATMOS_P,H,47.38,P,ENV_OUTSIDE_H*32
$IIXDR,C,,C,ENV_WATER_T,C,28.69,C,ENV_OUTAIR_T,P,101400,P,ENV_ATMOS_P*69

The program uses a user defined dictionary to associate the name field with a "Signal K path". The "name" field must match the 4th field in the XDR message in order for the XDR message to be processed.

The dictionary also includes an "expression" field which allows some algebraic conditioning of the data - for example to convert from the native numeric units to SI units. The "x" in the expression field relates to the contents of the data field in the XDR message. In the example below a conversion form Celcius to Kelvin is applied. 

This is an example of a sensor definition in the dictionary:

			{type = "Air temperature";  // "Vessel attitude" is a special value to identify attitude "objects"
			data = "temperature";	// descriptive only
			units = "float";	// describe the data output - options = int, float, radians
			name = "ENV_OUTAIR_T";	// used to match the 4th field in a XDR quadruple
			expression = "(x+273.15)";	// used to change units from native to SI
			sk_path ="environment.outside.temperature" },// the destination path in the Signal K delta format

There would be one definition for each quadruple in an XDR message. Without a definition the quadruple is ignored. 

The program uses Tinyexpr for the algebraic expression engine
https://github.com/codeplea/tinyexpr

It also uses Libconfig for reading the Dictionary file
https://github.com/hyperrealm/libconfig

To build and install on any debian based Linux distribution (sudo) but should build on an Linux distro. You will require the build environment.

1) Download the files (Clone or zip)
2) Extract as neccesary
3) Follow these instructions to install libconfig https://github.com/hyperrealm/libconfig/blob/master/INSTALL
4) Open a terminal in the xdr2sk directory
5) type "make" and hit the return key - this should build the xdr2sk binary
6) type "sudo make install" and hit return - this should install the xdr2sk binary into /usr/bin/

To remove the installation type "sudo make clean" and hit return. 

Before running xdr2sk edit the Dictionary.cfg as required. The program requires both TCP input and outputs to be defined. Something like kplex serves as an input. You will need to have this configured and the matching values configured in the dictionary example:
TCP_in_address = "127.0.0.1";
TCP_in_port = 10300;

Also your signal K server should have a connection configured and the details should match the settings in the dictionary.
TCP_out_address = "gary-laptop.local";
TCP_out_port = 10250;

It is not critical that you have XDR messages defined in the Dictionary at this point, the program will run but other than some conversation between it and the Signal K server no usable data will be generated.

To run as a service (systemd must be installed)
1) copy the "xdr2sk.service" file to /etc/systemd/system/ (sudo cp xdr2sk.service /etc/systemd/system/)
2) enable the service (sudo systemctl enable xdr2sk.service)

Have fun.

