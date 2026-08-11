/* Record format of the host database in TCPDIR/hostaddr and TCPDIR/hostname.
 *
 * It is written by util/mkhostdb and read by src/domain.c, so the layout has
 * to be defined in exactly one place.  It was not: mkhostdb stored a "long"
 * and domain.c read an "int32", which happened to agree until long became 64
 * bits.  After that the value in hostaddr was read four bytes short - which
 * still worked on a little endian machine - and the key in hostname was eight
 * bytes where the lookup used four, so the reverse direction, address to
 * name, stopped matching anything at all.
 *
 * The format is now explicit and independent of word size and byte order:
 *
 *      byte 0      family: 4 for IPv4, 6 for IPv6
 *      byte 1..n   the address in network byte order, 4 or 16 bytes
 *
 * hostaddr maps a name (string including the NUL) to such a record,
 * hostname maps such a record back to a name.
 */

#ifndef _HOSTDB_H
#define _HOSTDB_H

#define HOSTDB_V4       4
#define HOSTDB_V6       6
#define HOSTDB_RECLEN   17      /* largest record: tag plus 16 address bytes */

/* Build a record from a family (HOSTDB_V4/V6) and the address bytes in
 * network byte order.  Returns the record length, or 0 for an unknown family.
 */
int hostdb_encode(int family, const unsigned char *addr, unsigned char *rec);

/* Take a record apart.  Stores the family in *family and the address bytes in
 * addr, which must have room for 16.  Returns the address length in bytes, or
 * 0 if the record is not one we understand.
 *
 * Records of 4 or 8 bytes are accepted as well: those are the pre-existing
 * IPv4 layout, an int32 or a long in host byte order.  That keeps a database
 * built by an older mkhostdb usable until it is regenerated.
 */
int hostdb_decode(const unsigned char *rec, int reclen, int *family,
	unsigned char *addr);

#endif  /* _HOSTDB_H */
