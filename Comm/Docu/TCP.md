CommTCP

Driver for CommStreams.
It implements TCP/IP streams, using the operating system's sockets interface. It provides the NewListener and NewStream factory functions as needed for CommStreams. Do not import CommTCP directly; instead, use CommStreams and specify "CommTCP" as the protocol name.

Parameters localAdr and remoteAdr
The CommStreams procedures NewStream and NewListener feature string parameters remoteAdr and localAdr. The interpretation of the strings passed depends on the implementation of the actual driver module. For CommTCP the following holds.
The remoteAdr (only used with NewStream) must be either an IP address or a host's name, followed by a colon (":") and a port number. (Examples: "127.0.0.1:2", "loopback:2".)
The parameter localAdr identifies the port on which a listener is established (NewListener) or from which a remote connection is attempted (NewStream). Valid values are
-	an empty string
-	an IP address or a host's name (Examples: "127.0.0.1", "loopback")
-	an IP address or a host's name followed by a colon (":") and a port number
(Examples: "127.0.0.1:2", "loopback:2")
-	a port number (Example: "2")
For unspecified port numbers, a wildcard is used, directing the operating system to choose freely. For unspecified local addresses the local host's address "127.0.0.1" is used.
The special IP address "0.0.0.0" means all available IP addresses on the local machine (including "127.0.0.1"). This can be used for listening on all available network interfaces or for connecting to a server using any available network interface. In this case network traffic may leave the local machine.