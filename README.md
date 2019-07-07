# xdr2sk
A NMEA0183 to Signal K converter

This program converts NMEA0183 XDR messages to Signal K delta format.
It is C based and should compile on an Linux platform

It uses TCP sockets for both input and output. It is assumed that the NMEA XDR messages will originate from something like kplex.
The input side is therefore a TCP client.
The Signal K server supports a TCP connection to a TCP server, therefore the output side is a TCP server.

The address and port details are defined in the dictionary - see below.

XDR messages are used to provide data relating to transducers. They have a reputation for being ad hoc and are somewhat unsupported - hence this program. XDR messages are made up of one or more quadruples. The second field in the quadruple contains the data and the 4th field is the name field. 

Here are examples:

$IIXDR,C,28.7,C,ENV_OUTSIDE_T,P,101400,P,ENV_ATMOS_P,H,47.38,P,ENV_OUTSIDE_H*32
$IIXDR,C,,C,ENV_WATER_T,C,28.69,C,ENV_OUTAIR_T,P,101400,P,ENV_ATMOS_P*69

The program uses a user defined dictionary to associate the name field with a "Signal K path". The "name" field must match the 4th field in the XDR message in order for the XDR message to be processed.

The dictionary also includes an "expression" field which allows some algebraic conditioning of the data - for example to convert from the native numeric units to SI units. The "x" in the expression field relates to the data in the data field in the XDR message. In the example below a conversion form Celcius to Kelvin is applied. 

This is an example of a sensor definition in the dictionary:

			{type = "Air temperature";
			data = "temperature";
			units = "float";
			name = "ENV_OUTAIR_T";
			expression = "(x+273.15)";
			sk_path ="environment.outside.temperature" },

The program uses Tinyexpr for the algebraic expression engine
https://github.com/codeplea/tinyexpr

It also uses Libconfig for reading the Dictionary file
https://github.com/hyperrealm/libconfig
