/*
 * Copyright (c) 2014 Franco Fichtner <franco@packetwerk.com>
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

#ifndef PEAK_TRANSFER_H
#define PEAK_TRANSFER_H

struct peak_transfer {
	struct peak_timeval ts;
	const char *ifname;
	void *private_data;
	unsigned int type;
	unsigned int len;
	unsigned int ll;
	unsigned int id;
	void *buf;
};

struct peak_transfers {
	unsigned int (*send)(struct peak_transfer *, const char *,
	    const unsigned int);
	struct peak_transfer *(*recv)(struct peak_transfer *, int,
	    const char *, const unsigned int);
	unsigned int (*forward)(struct peak_transfer *);
	unsigned int (*attach)(const char *);
	unsigned int (*detach)(const char *);
	unsigned int (*master)(const char *);
	unsigned int (*slave)(const char *);
	void (*unlock)(void);
	void (*lock)(void);
};

static inline unsigned int
peak_transfer_send(struct peak_transfer *x, const char *y,
    const unsigned int z)
{
	(void)x;
	(void)y;
	(void)z;

	return (1);
}

static inline struct peak_transfer *
peak_transfer_recv(struct peak_transfer *x, int y, const char *z,
    const unsigned int a)
{
	(void)x;
	(void)y;
	(void)z;
	(void)a;

	return (NULL);
}

static inline unsigned int
peak_transfer_forward(struct peak_transfer *x)
{
	(void)x;

	return (1);
}

static inline unsigned int
peak_transfer_master(const char *x)
{
	(void)x;

	return (1);
}

static inline unsigned int
peak_transfer_slave(const char *x)
{
	(void)x;

	return (1);
}

static inline unsigned int
peak_transfer_attach(const char *x)
{
	(void)x;

	return (1);
}

static inline unsigned int
peak_transfer_detach(const char *x)
{
	(void)x;

	return (1);
}

static inline void
peak_transfer_unlock(void)
{
}

static inline void
peak_transfer_lock(void)
{
}

#endif /* PEAK_TRANSFER_H */
