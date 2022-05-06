#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER 2000
#define ACK_TIMER 1100
#define MAX_SEQ 7

#define inc(x) x = (x == MAX_SEQ) ? 0 : x + 1;

typedef unsigned char seq_nr;
typedef unsigned char frame_kind;
typedef unsigned char packet[PKT_LEN];

struct frame{
	frame_kind kind; /* FRAME_DATA */
	seq_nr ack;
	seq_nr seq;
	packet info[PKT_LEN];
	unsigned int checksum;
};

static int phl_ready = 0;

static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static void send_data_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	s.ack = 1 - frame_expected;
	memcpy(s.data, buffer, PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr, DATA_TIMER);
}

static void send_ack_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = 1 - frame_expected;

	dbg_frame("Send ACK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
	int event, arg;
	int len = 0;

	struct frame r;
	seq_nr next_frame_to_send;
	seq_nr frame_expected;
	seq_nr ack_expected;
	packet buffer[MAX_SEQ + 1];
	seq_nr nbuffered;

	protocol_init(argc, argv);
	lprintf("Designed by Wu Qingliu, build: "__DATE__"  "__TIME__"\n");

	disable_network_layer();

	next_frame_to_send = 0;
	frame_expected = 0;
	ack_expected = 0;
	nbuffered = 0;

	for (;;) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			get_packet(buffer[next_frame_to_send]);
			nbuffered++;
			send_data(FRAME_DATA, next_frame_to_send, frame_expected, buffer);
			inc(next_frame_to_send);
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&r, sizeof r);
			/* the minimum length with crc checksum is 7 bytes */
			if (len < 7 || crc32((unsigned char *)&r, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				break;
			}

			if (r.kind == FRAME_DATA) {
				dbg_frame("Recv DATA %d %d, ID %d, %d bytes\n", r.seq, r.ack, *(short *)r.info, len);
				if (r.seq == frame_expected) {
					/* sent do network layer with header and tail removed */
					put_packet(r.info, len - 7);
					inc(frame_expected);
					start_ack_timer(ACK_TIMER);
				} else {
					dbg_frame("Frame with wrong seq_nr %d received.\n", r.seq);
				}
			}

			if (r.kind == FRAME_ACK)
				dbg_frame("Recv ACK %d\n", f.ack);
				
			while(between(ack_expected, r.ack, next_frame_to_send)) {
				nbuffered--;
				stop_timer(ack_expected);
				inc(ack_expected);
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			next_frame_to_send = ack_expected;
			for(int i = 0; i < nbuffered; i++) {
				send_data(FRAME_DATA, next_frame_to_send, frame_expected, buffer);
				inc(next_frame_to_send);
			}
			break;

		/* No data to send from network layer until ack timer expired. */
		case ACK_TIMEOUT:
			/* next_frame_to_send and buffer is meaningless */
			send_data(FRAME_ACK, 0, frame_expected, buffer);
			break;
		
		default:
			dbg_event("Unhandled cased");
			break;
		}

		if (nbuffered < MAX_SEQ && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}