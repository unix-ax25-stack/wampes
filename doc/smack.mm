.\"
.\" Format this manual with:
.\"
.\" groff -t -p -mgm manual.mm
.\"
.PH "" \" Page header
.ds HF 3 3 3 3 3 3 3 \" All headers bold
.ds HP +2 +2 +2 +2 +2 +2 +2 \" All headers 2 points bigger
.nr Cl 7 \" Max level of header for table of contents
.nr Ej 1 \" Level 1 headers on new page
.nr Hb 7 \" Break after all headers
.nr Hs 7 \" Empty line after all headers
.nr Hy 1 \" Hyphenation on
.\"
.PF "^The SMACK Protocol^-\\\\nP-^Version 1.0" \" Page footer
.\"
.S 30
.ce
\fBThe SMACK Protocol\fP
.ce
Version 1.0, (27 February 1992)
.S
.SP 2
.S 15
.ce
by Jan Schiefer, DL5UE and Dieter Deyke, DK5SG/N0PRA
.SP
.ce
English translation by Mike Chace (G6DHU) February 1993
.S
.nr Ej 0 \" No new page for all headers
.H 1 "Introduction"
.P
At the end of 1990, the Stuttgart Packet Radio Group began to think
in earnest about the security of data between a TNC and a WAMPES [*] node.
It was noted that many other Packet node systems suffered
from loss of data on the serial lines and effort was put into
devising an extension of the KISS protocol to overcome this.
.P
The result of this development was SMACK (Stuttgart's Modified
Amateur CRC KISS). This document introduces SMACK and its differences
over normal KISS so that implementations for other systems can be
developed.
.H 1 "What Is KISS ?"
.P
The KISS protocol was developed in 1986 by Phil Karn (KA9Q) and Mike
Cheppionis (K3MC) [1]. The need for KISS was born out of the
requirement for a simple interface from TCP/IP software to the AX.25
Protocol environment. KISS provides a level 2a protocol interface. It
provides channel access through CSMA/CD and the P-Persistence algorithm
together with the conversion from synchronous HDLC data on the
channel to asynchronous data on an RS232 serial channel.
.P
KISS organizes serial "packets" through a known delimiter and defines
a simple command set enabling TNC parameters to be set. Later,
improvements to the protocol were devised allowing a single serial
channel to share data from more than one Packet Radio channel.
.H 1 "Modifications of the KISS Protocol"
.P
The host computer communicates with KISS in the form of packets. The
start of each packet is recognized through the FEND delimiter. The
Command Byte then follows. This determines whether the rest of the
packet contains a command or data from the HDLC channel. Apart from
the Reset Command ($FF), all commands use only the lower 4 bits for
the Command Byte. It is this property that allows multi channel TNCs
to share the same serial channel through using the upper 4 bits as
the HDLC channel number. This allows up to 16 HDLC channels to be
supported.
.P
Since we knew of no 16 or even 8 channel TNCs, we decided to use the
uppermost bit of the Command Byte. If it is set, this signifies that
packets with CRC follow. It is important to note that it is only data
packets that carry this checksum. The lowest byte of the checksum is
received first.
.P
The following diagrams show the frame format of data packets, one
with checksum and one without:
.DS

+------+------+------+------+-----+------+------+
| FEND | 0x00 | DATA | DATA | ... | DATA | FEND |
+------+------+------+------+-----+------+------+

KISS data frame (without checksum)

.\"

+------+------+------+------+----
| FEND | 0x80 | DATA | DATA | ...
+------+------+------+------+----

		  ---+------+---------+----------+------+
		 ... |DATA  | CRC LOW | CRC HIGH | FEND |
		  ---+------+---------+----------+------+

SMACK data frame (with checksum)
.DE
.P
At this point, it should be reiterated that only data frames are
secured with the checksum. This prevents command frames being sent to
the TNC when Host computer and TNC are not in agreement over whether
CRC's are in use.
.H 1 "Switching from KISS to SMACK"
.P
At startup, a SMACK-capable TNC is in normal KISS mode and therefore
does not generate checksums. As soon as the first frame with CRC
arrives, the SMACK send routine is invoked. From this point onwards,
SMACK can only be exited through a reset. This is also the case for
the host computer.
.P
If however, a KISS TNC is present at the end of the serial link,
frames from the host containing a CRC will be decoded as unknown
Command Bytes and are therefore discarded. KISS operation then
continues as normal. This has the advantage that normal and SMACK-capable
KISS TNCs can operate using the same host. No changes in host
configuration are required.
.P
The KISS/SMACK negotiations are illustrated below:
.DS
---------------------------------------------------------------
Example 1: KISS-TNC

	    Host                            TNC

- Sends a single packet with
  CRC, returns its send routine
  back to normal KISS mode.

				- Receives a frame which for
				  it appears to be the
				  unknown Command Byte 0x80
				  which it duly discards.

- Sends KISS data without
  Checksums.

				- Sends KISS data without
				  Checksums.

---------------------------------------------------------------
.DE
.P
Example 2: SMACK-TNC
.DS
	      Host                          TNC

- Sends a single frame with CRC
  and switches its send routine
  back to normal KISS mode.

				- Receives a frame with CRC
				  signature, switches its
				  send routine into CRC mode.

- Sends KISS data without any
  Checksums.

				- TNC sends the first frame
				  with CRC.

- Receives a frame with the CRC
  signature, switches its send
  routine into CRC mode.

- Sends all data in SMACK mode
  with Checksums.

				- Sends all data in SMACK
				  mode with Checksums.

---------------------------------------------------------------
.DE
.P
Regardless of the sender's state (CRC/no CRC), received frames are
always handled according to the following rules.
.P
.DS
    Received Frame        | Action
--------------------------+--------------
Without CRC               | Process Frame
With CRC, Checksum OK     | Process Frame
With CRC, Checksum Bad    | Discard Frame
.DE
.P
These rules assume that all KISS implementation discard unrecognized
frames. This assumption is stated in the KISS Protocol Specification
[1].
.H 1 "CRC Calculation and Implementation Tips"
.P
This is not the right forum in which to discuss the theory of the Cyclic
Redundancy Check (CRC). For this information read the work of Michael
Roehner, DC4OX [2]. This excerpt merely discusses the essential
points that allow other implementations to be developed.
.P
The check polynomial is the standard CRC16 Polynomial. This has the
formula:
.SP 1
.DS
	 16    15   2
	X   + X  + X  + 1
.DE
.P
The CRC generator is primed with 0. The CRC is calculated over all
data frames including the Command Byte 0x80.
.P
As discussed before, the KISS protocol delimits frames with the FEND
character (0xc0). In the event that this character is present in the
data portion of the frame, this is handled by the software. This
strategy is called SLIP (Serial Line Internet Protocol) encoding.
.P
The CRC must be calculated before SLIP encoding is carried out and
checked after SLIP decoding is complete. This is for a number of
reasons:
.SP 1
.BL
.LI
The CRC bytes could themselves contain the FESC, FEND and TFEND
"special" KISS characters.
.LI
The SLIP encoder/decoder is in most host implementations (eg NOS,
WAMPES), used independently of KISS.
.LE
.P
The CRCs belong logically to the KISS layer.
.P
The calculation algorithm is as follows:
.AL
.LI
CRC generator is primed with 0.
.LI
Stuff all data bytes into the algorithm including the two CRC bytes.
.LI
At the end of the calculation, 0's must again be present in the CRC
generator. If this is not the case, a corruption must have occurred
and the frame must be discarded.
.LE
.P
Various calculation algorithms for CRC generators are discussed in
[2]. Here is a simple table-driven generator written in the C
language, which generates the CRC of a buffer (buf) of length n.
.DS
/*-------------------------------------------------------*/

static int  calc_crc(char *buf, int n)
{

  static int  crc_table[] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
    0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
    0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
    0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
    0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
    0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
    0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
    0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
    0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
    0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
    0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
    0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
    0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
    0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
    0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
    0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
    0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
    0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
    0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
    0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
    0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
    0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
    0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
    0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
    0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
    0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
    0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
    0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
    0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
    0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
    0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
    0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040
  };

  int  crc;

  crc = 0;
  while (--n >= 0)
    crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ *buf++) & 0xff];
  return crc;
}

/*--------------------------------------------------------*/
.DE
.P
The table requires some 512 bytes of memory. If this is too great for
a simple TNC EPROM implementation, the table can be built up at
runtime as in the following example:
.SP 1
.DS
/*--------------------------------------------------------*/

unsigned short Table[256];
const int Rest[8] = { 0xC0C1, 0xC181, 0xC301, 0xC601, 0xCC01,
		      0xD801, 0xF001, 0xA001 };
main()
{
	int i, j;
	unsigned short value;

	for (i = 0; i < 256; i++) {
		value = 0;
		for (j = 0; j < 8; j++)
			if (i & (1 << j))
				value ^= Rest[j];
		Table[i] = value;
	}
}

/*----------------------------------------------------------*/
.DE
.P
If this algorithm is coded in assembler, it requires less space than
that taken by the table itself. The theory can be found in [2].
.H 2 "Implementations"
.P
The SMACK protocol has been implemented in the following systems:
.AL
.LI
The WAMPES TCP/IP software
.LI
SMACK, version 1.3. This software was developed by Jan Schiefer,
DL5UE from the original TNC2 KISS implementation by K3MC. This has
been used in the TNCs of the WAMPES network nodes DB0ID (Stuttgart)
and DB0SAO (Stuttgart Mailbox).
.LI
In NORD><LINK's "The Firmware", Multi Channel TNC software for TNC2
and compatibles from version 2.4 onwards.
.LI
Tommy Osterried, DL9SAU has implemented a SMACK patch that should be
suitable for the NOS TCP/IP software variant.
.LE
.H 1 "The Future ?"
.P
One development on the wish list would be an implementation of the
SMACK protocol in the standard Packet Driver format [3]. This would
then allow any existing version of NOS or NET to use SMACK without
any changes to the source code. In any case, all Packet Radio
software can benefit from the extra security that SMACK affords.
.P
.H 1 "References"
.VL 10 2 1
.LI [*]
WAMPES = Wuerttembergische Amateur Multi-Protocol-Experimental-
Software. This is TCP/IP package that runs under various flavors
of the UNIX operating system. It supports AX.25, NET/ROM and TCP/IP.
It has been installed primarily in the Stuttgart area of Germany and
provides the basic network node functions for that region. See also
[4].
.LI [1]
Karn, Phil, KA9Q; Proposed "Raw" TNC Functional Spec, 6.8.1986;
Available in many UseNet news archives.
.LI [2]
Roehner, Michael, DC4OX; What is CRC ?
Available throughout the German Mailbox network, May 1988
.LI [3]
FTP Software, Inc.; PC/TCP Version 1.09 Packet Driver Specification;
Wakefield, MA 1989
.LI [4]
Schiefer, Jan, DL5UE; WAMPES - Further Developments; Speech given to
the 5th regional Packet Radio Meeting; Frankfurt 1989;
.LE
.TC 1 1 7 0
