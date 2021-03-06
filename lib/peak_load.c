/*
 * Copyright (c) 2012-2014 Franco Fichtner <franco@packetwerk.com>
 * Copyright (c) 2013 Masoud Chelongar <masoud@packetwerk.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fcntl.h>
#include <peak.h>
#include <unistd.h>

#define LOAD_FROM_USER(x)	(((struct _peak_load *)((x) + 1)) - 1)
#define LOAD_TO_USER(x)		&(x)->data

struct _peak_load {
	uint32_t *netmon_table;
	uint32_t netmon_count;
	uint32_t netmon_pos;
	time_t netmon_time;
	unsigned int fmt;
	int fd;
	struct peak_load data;
};

struct erf_packet_header {
	uint64_t ts;
	uint8_t type;
	uint8_t flags;
	uint16_t rlen;
	uint16_t lctr;
	uint16_t wlen;
	uint16_t wtf;
} __packed;

#define ERF_USEC(x)	((((x) & 0xFFFFFFFFull) * 1000ull * 1000ull) >> 32)
#define ERF_SEC(x)	((x) >> 32)
#define ERF_MS(x)	((((x) >> 32) * 1000) +				\
    ((((x) & 0xFFFFFFFF) * 1000) >> 32))
#define ERF_MAGIC	0	/* there is no ERF magic; it's a stub */

struct pcap_file_header {
	uint32_t magic_number;
	uint16_t version_major;
	uint16_t version_minor;
	int32_t thiszone;
	uint32_t sigfigs;
	uint32_t snaplen;
	uint32_t network;
};

struct pcap_packet_header {
	uint32_t ts_sec;
	uint32_t ts_usec;
	uint32_t incl_len;
	uint32_t orig_len;
};

#define PCAP_MS(x, y)	((int64_t)(x) * 1000 + (int64_t)(y) / 1000)
#define PCAP_SWAP_MAGIC	0xD4C3B2A1
#define PCAP_MAGIC	0xA1B2C3D4

struct pcapng_block_header {
	uint32_t type;
	uint32_t length;
};

struct pcapng_iface_desc_header {
	uint16_t linktype;
	uint16_t reserved;
	uint32_t snaplen;
};

struct pcapng_enhanced_pkt_header {
	uint32_t interface_id;
	uint32_t ts_high;
	uint32_t ts_low;
	uint32_t capturelen;
	uint32_t packetlen;
};

#define PCAPNG_MAGIC	0x0A0D0D0A

struct netmon_file_header {
	uint32_t magic;
	uint8_t version_minor;
	uint8_t version_major;
	uint16_t network;
	uint16_t start_year;
	uint16_t start_month;
	uint16_t start_dow;
	uint16_t start_day;
	uint16_t start_hour;
	uint16_t start_min;
	uint16_t start_sec;
	uint16_t start_msec;
	uint32_t frame_table_offset;
	uint32_t frame_table_length;
	uint32_t user_data_offset;
	uint32_t user_data_length;
	uint32_t comment_data_offset;
	uint32_t comment_data_length;
	uint32_t statistics_offset;
	uint32_t statistics_length;
	uint32_t network_info_offset;
	uint32_t network_info_length;
};

struct netmon_record_header {
	uint64_t time_stamp_data;
	uint32_t original_length;
	uint32_t include_length;
};

#define NETMON_USEC(x)	((((x) * 10ull) / 10ull) % (1000ull * 1000ull))
#define NETMON_SEC(x)	(((x) * 10ll) / (10ll * 1000ll * 1000ll))
#define NETMON_MAGIC	0x55424D47

static void
_peak_load_erf(struct _peak_load *self)
{
	struct erf_packet_header hdr;
	ssize_t ret;

	ret = read(self->fd, &hdr, sizeof(hdr));
	if (ret != sizeof(hdr)) {
		return;
	}

	hdr.rlen = be16dec(&hdr.rlen) - sizeof(hdr);
	hdr.wlen = be16dec(&hdr.wlen);

	if (sizeof(self->data.buf) < hdr.rlen) {
		return;
	}

	ret = read(self->fd, self->data.buf, hdr.rlen);
	if (ret != hdr.rlen) {
		return;
	}

	self->data.ts.tv_usec = ERF_USEC(hdr.ts);
	self->data.ts.tv_sec = ERF_SEC(hdr.ts);
	self->data.len = hdr.wlen;
}

static void
_peak_load_pcap(struct _peak_load *self)
{
	struct pcap_packet_header hdr;
	ssize_t ret;

	ret = read(self->fd, &hdr, sizeof(hdr));
	if (ret != sizeof(hdr)) {
		return;
	}

	if (self->fmt == PCAP_SWAP_MAGIC) {
		hdr.incl_len = bswap32(hdr.incl_len);
		hdr.orig_len = bswap32(hdr.orig_len);
		hdr.ts_usec = bswap32(hdr.ts_usec);
		hdr.ts_sec = bswap32(hdr.ts_sec);
	}

	if (sizeof(self->data.buf) < hdr.incl_len) {
		return;
	}

	ret = read(self->fd, self->data.buf, hdr.incl_len);
	if (ret != hdr.incl_len) {
		return;
	}

	self->data.ts.tv_usec = hdr.ts_usec;
	self->data.ts.tv_sec = hdr.ts_sec;
	self->data.len = hdr.incl_len;
}

static void
_peak_load_pcapng(struct _peak_load *self)
{
	struct pcapng_enhanced_pkt_header pkt;
	struct pcapng_iface_desc_header iface;
	struct pcapng_block_header hdr;
	ssize_t ret;
	int64_t ts;

_peak_load_pcapng_again:

	ret = read(self->fd, &hdr, sizeof(hdr));
	if (ret != sizeof(hdr)) {
		return;
	}

	if (sizeof(self->data.buf) < hdr.length) {
		return;
	}

	switch (hdr.type) {
	case 2: /* packet block (obsolete) */
	case 6:	/* enhanced packet block */
		ret = read(self->fd, &pkt, sizeof(pkt));
		if (ret != sizeof(pkt)) {
			return;
		}

		ret = read(self->fd, self->data.buf, hdr.length -
		    sizeof(hdr) - sizeof(pkt));
		if (ret != (ssize_t)(hdr.length -
		    sizeof(hdr) - sizeof(pkt))) {
			return;
		}

		ts = (int64_t)pkt.ts_high << 32 | (int64_t)pkt.ts_low;

		self->data.ts.tv_usec = ts % (1000ll * 1000ll);
		self->data.ts.tv_sec = ts / 1000ll / 1000ll;
		self->data.len = pkt.packetlen;

		break;
	case 1:	/* interface description block */
		ret = read(self->fd, &iface, sizeof(iface));
		if (ret != sizeof(iface)) {
			return;
		}

		lseek(self->fd, hdr.length - sizeof(hdr) - sizeof(iface),
		    SEEK_CUR);

		self->data.ll = iface.linktype;

		goto _peak_load_pcapng_again;
	default:
		/*
		 * The blocks we don't know we don't look at more
		 * closely.  A new section may begin here as well,
		 * but we mop up the link type change above anyway.
		 */
		lseek(self->fd, hdr.length - sizeof(hdr), SEEK_CUR);
		goto _peak_load_pcapng_again;
	}
}

static void
_peak_load_netmon(struct _peak_load *self)
{
	struct netmon_record_header hdr;
	ssize_t ret;

	if (self->netmon_pos == self->netmon_count) {
		/* all packets are read */
		return;
	}

	lseek(self->fd, self->netmon_table[self->netmon_pos++], SEEK_SET);

	ret = read(self->fd, &hdr, sizeof(hdr));
	if (ret != sizeof(hdr)) {
		return;
	}

	if (sizeof(self->data.buf) < hdr.include_length) {
		return;
	}

	ret = read(self->fd, self->data.buf, hdr.include_length);
	if (ret != hdr.include_length) {
		return;
	}

	self->data.ts.tv_usec = NETMON_USEC(hdr.time_stamp_data);
	self->data.ts.tv_sec = NETMON_SEC(hdr.time_stamp_data) +
	    self->netmon_time;
	self->data.len = hdr.original_length;
}

unsigned int
peak_load_packet(struct peak_load *self_user)
{
	struct _peak_load *self = LOAD_FROM_USER(self_user);

	self->data.len = 0;

	switch (self->fmt) {
	case ERF_MAGIC:
		_peak_load_erf(self);
		break;
	case PCAP_SWAP_MAGIC:
	case PCAP_MAGIC:
		_peak_load_pcap(self);
		break;
	case PCAPNG_MAGIC:
		_peak_load_pcapng(self);
		break;
	case NETMON_MAGIC:
		_peak_load_netmon(self);
		break;
	}

	return (self->data.len);
}

struct peak_load *
peak_load_init(const char *file)
{
	unsigned int ll = LINKTYPE_ETHERNET;
	struct _peak_load *self = NULL;
	uint32_t *netmon_table = NULL;
	unsigned int fmt = ERF_MAGIC;
	uint32_t netmon_length = 0;
	time_t netmon_time = 0;
	int fd = STDIN_FILENO;

	if (file) {
		uint32_t file_magic;

		fd = open(file, O_RDONLY);
		if (fd < 0) {
			goto peak_load_init_out;
		}

		/* autodetect format if possible */
		if (read(fd, &file_magic, sizeof(file_magic)) !=
		    sizeof(file_magic)) {
			goto peak_load_init_close;
		}

		/* rewind & pretend nothing happened */
		lseek(fd, 0, SEEK_SET);

		switch (file_magic) {
		case PCAP_SWAP_MAGIC:
		case PCAP_MAGIC: {
			struct pcap_file_header hdr;

			if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
				goto peak_load_init_close;
			}

			fmt = PCAP_MAGIC;

			if (file_magic == PCAP_SWAP_MAGIC) {
				hdr.magic_number =
				    bswap32(hdr.magic_number);
				hdr.version_major =
				    bswap16(hdr.version_major);
				hdr.version_minor =
				    bswap16(hdr.version_minor);
				hdr.thiszone = bswap32(hdr.thiszone);
				hdr.sigfigs = bswap32(hdr.sigfigs);
				hdr.snaplen = bswap32(hdr.snaplen);
				hdr.network = bswap32(hdr.network);
				fmt = PCAP_SWAP_MAGIC;
			}

			ll = hdr.network;

			break;
		}
		case NETMON_MAGIC: {
			struct netmon_file_header hdr;
			struct tm base;

			if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
				goto peak_load_init_close;
			}

			if (hdr.version_minor != 0 ||
			    hdr.version_major != 2) {
				/* can only support version 2.0 for now */
				goto peak_load_init_close;
			}

			if (hdr.network > 1) {
				/*
				 * Sorry for magic constant.  Both 0 and 1
				 * are Ethernet packets.  The value is set
				 * automatically.  Other link layers are
				 * not supported.
				 */
				goto peak_load_init_close;
			}

			netmon_length = hdr.frame_table_length;
			netmon_table = malloc(netmon_length);
			if (!netmon_table) {
				goto peak_load_init_close;
			}

			memset(&base, 0, sizeof(base));

			/* convert to time_t */
			base.tm_year = hdr.start_year - 1900;
			base.tm_mon = hdr.start_month - 1;
			base.tm_mday = hdr.start_day;
			base.tm_hour = hdr.start_hour;
			base.tm_min = hdr.start_min;
			base.tm_sec = hdr.start_sec;

			netmon_time = timegm(&base);

			lseek(fd, hdr.frame_table_offset, SEEK_SET);

			if (read(fd, netmon_table, netmon_length) !=
			    netmon_length) {
				goto peak_load_init_close;
			}

			fmt = NETMON_MAGIC;

			break;
		}
		case PCAPNG_MAGIC: {
			struct pcapng_block_header hdr;

			if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
				goto peak_load_init_close;
			}

			/* skip over section header block */
			lseek(fd, hdr.length - sizeof(hdr), SEEK_CUR);

			/* change link type on the fly later */
			fmt = PCAPNG_MAGIC;
			ll = 0;

			break;
		}
		default:
			/* unknown magic means erf format */
			break;
		}
	}

	self = calloc(1, sizeof(*self));
	if (!self) {
		goto peak_load_init_close;
	}

	self->netmon_count = netmon_length / sizeof(*netmon_table);
	self->netmon_table = netmon_table;
	self->netmon_time = netmon_time;
	self->fmt = fmt;
	self->fd = fd;

	self->data.ll = ll;

	return (LOAD_TO_USER(self));

  peak_load_init_close:

	if (fd != STDIN_FILENO) {
		close(fd);
	}

	free(netmon_table);

  peak_load_init_out:

	return (NULL);
}

void
peak_load_exit(struct peak_load *self_user)
{
	if (self_user) {
		struct _peak_load *self = LOAD_FROM_USER(self_user);
		if (self->fd != STDIN_FILENO) {
			close(self->fd);
		}
		free(self->netmon_table);
		free(self);
	}
}
