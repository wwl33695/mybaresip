/**
 * @file sip_ua.c SIP User Agent Demo
 *
 *
 *  To test inbound calls, invoke without arguments. sip_ua will
 *  register and wait for an inbound call:
 *
 *  $ ./sip_ua
 *
 *
 *  To test outbound calls, invoke with a SIP URI argument.
 *  sip_ua will invite provided URI:
 *
 *  $ ./sip_ua sip:echo@creytiv.com
 *
 *
 * Copyright (C) 2011 Creytiv.com
 */

#define __ssize_t_defined

#include <string.h>
#include <re/re.h>


static struct sipsess_sock *sess_sock;  /* SIP session socket */
static struct sdp_session *sdp;         /* SDP session        */
static struct sdp_media *sdp_media, *sdp_media_, *sdp_media1;     /* SDP media          */
static struct sipsess *sess;            /* SIP session        */
static struct sipreg *reg;              /* SIP registration   */
static struct sip *sip;                 /* SIP stack          */
static struct rtp_sock *rtp, *rtp1, *rtp_, *rtp1_;            /* RTP socket         */
static struct sip_lsnr *lsnr;
struct dnsc *dnsc = NULL;

const char *registrar  = "sip:creytiv.com";
const char *uri  = "sip:demo@creytiv.com";
const char *name = "demo";


/* terminate */
static void terminate(void)
{
	/* terminate session */
	sess = mem_deref(sess);

	/* terminate registration */
	reg = mem_deref(reg);

	/* wait for pending transactions to finish */
	sip_close(sip, false);
}


/* called for every received RTP packet */
static void rtp_handler(const struct sa *src, const struct rtp_header *hdr,
			struct mbuf *mb, void *arg)
{
	(void)hdr;
	(void)arg;
	struct rtp_sock * rtpsock = *(struct rtp_sock **)arg;

//	re_printf("rtp: recv %zu bytes from %J\n", mbuf_get_left(mb), src);

	rtp_send(rtpsock, src, false, hdr->m, hdr->pt, hdr->ts, mb);

	if( rtpsock == rtp)
	{
		mbuf_advance(mb, RTP_HEADER_SIZE);
		rtp_send(rtp_, src, false, hdr->m, hdr->pt, hdr->ts, mb);
	}
	else if( rtpsock == rtp1 )
	{
		mbuf_advance(mb, RTP_HEADER_SIZE);
		rtp_send(rtp1_, src, false, hdr->m, hdr->pt, hdr->ts, mb);
	}

}


/* called for every received RTCP packet */
static void rtcp_handler(const struct sa *src, struct rtcp_msg *msg, void *arg)
{
	(void)arg;

	re_printf("rtcp: recv %s from %J\n", rtcp_type_name(msg->hdr.pt), src);
}


/* called when challenged for credentials */
static int auth_handler(char **user, char **pass, const char *realm, void *arg)
{
	int err = 0;
	(void)realm;
	(void)arg;

	re_printf("auth_handler \n");

	err |= str_dup(user, "demo");
	err |= str_dup(pass, "secret");

	return err;
}


/* print SDP status */
static void update_media(void)
{
	const struct sdp_format *fmt;

	re_printf("SDP peer address: %J\n", sdp_media_raddr(sdp_media));

	fmt = sdp_media_rformat(sdp_media, NULL);
	if (!fmt) {
		re_printf("no common media format found\n");
		return;
	}

	re_printf("SDP media format: %s/%u/%u (payload type: %u)\n",
		  fmt->name, fmt->srate, fmt->ch, fmt->pt);

	re_printf("formatlist_count:local:%d, remote:%d\n", 
		list_count(sdp_media_format_lst(sdp_media, true)),
		list_count(sdp_media_format_lst(sdp_media, false)));
}


/*
 * called when an SDP offer is received (got offer: true) or
 * when an offer is to be sent (got_offer: false)
 */
static int offer_handler(struct mbuf **mbp, const struct sip_msg *msg,
			 void *arg)
{
	const bool got_offer = mbuf_get_left(msg->mb);
	int err;
	(void)arg;

	re_printf("offer_handler \n");

	if (got_offer) {

		err = sdp_decode(sdp, msg->mb, true);
		if (err) {
			re_fprintf(stderr, "unable to decode SDP offer: %s\n",
				   strerror(err));
			return err;
		}

		re_printf("SDP offer received\n");
		update_media();
	}
	else {
		re_printf("sending SDP offer\n");
	}

	return sdp_encode(mbp, sdp, !got_offer);
}


/* called when an SDP answer is received */
static int answer_handler(const struct sip_msg *msg, void *arg)
{
	int err;
	(void)arg;

	re_printf("SDP answer received\n");

	err = sdp_decode(sdp, msg->mb, false);
	if (err) {
		re_fprintf(stderr, "unable to decode SDP answer: %s\n",
			   strerror(err));
		return err;
	}

	update_media();

	return 0;
}


/* called when SIP progress (like 180 Ringing) responses are received */
static void progress_handler(const struct sip_msg *msg, void *arg)
{
	(void)arg;

	re_printf("session progress: %u %r\n", msg->scode, &msg->reason);
}


/* called when the session is established */
static void establish_handler(const struct sip_msg *msg, void *arg)
{
	(void)msg;
	(void)arg;

	re_printf("session established\n");
}


/* called when the session fails to connect or is terminated from peer */
static void close_handler(int err, const struct sip_msg *msg, void *arg)
{
	(void)arg;

	if (err)
		re_printf("session closed: %s\n", strerror(err));
	else
		re_printf("session closed: %u %r\n", msg->scode, &msg->reason);

	terminate();
}


/* called upon incoming calls */
static void connect_handler(const struct sip_msg *msg, void *arg)
{
	struct mbuf *mb;
	bool got_offer;
	int err;
	(void)arg;

	re_printf("connect_handler \n");

	sdp_session_debug((struct re_printf*)re_get_stdio_printf(), sdp);

	if (sess) {
		/* Already in a call */
		(void)sip_treply(NULL, sip, msg, 486, "Busy Here");
		return;
	}

	got_offer = (mbuf_get_left(msg->mb) > 0);

	/* Decode SDP offer if incoming INVITE contains SDP */

	if (got_offer) {

		err = sdp_decode(sdp, msg->mb, true);
		if (err) {
			re_fprintf(stderr, "unable to decode SDP offer: %s\n",
				   strerror(err));
			goto out;
		}

		update_media();
	}

	sdp_session_debug((struct re_printf*)re_get_stdio_printf(), sdp);

	/* Encode SDP */
	err = sdp_encode(&mb, sdp, !got_offer);
	if (err) {
		re_fprintf(stderr, "unable to encode SDP: %s\n",
			   strerror(err));
		goto out;
	}

	/* Answer incoming call */
	err = sipsess_accept(&sess, sess_sock, msg, 200, "OK",
			     name, "application/sdp", mb,
			     auth_handler, NULL, false,
			     offer_handler, answer_handler,
			     establish_handler, NULL, NULL,
			     close_handler, NULL, NULL);
	mem_deref(mb); /* free SDP buffer */
	if (err) {
		re_fprintf(stderr, "session accept error: %s\n",
			   strerror(err));
		goto out;
	}

 out:
	if (err) {
		(void)sip_treply(NULL, sip, msg, 500, strerror(err));
	}
	else {
		re_printf("accepting incoming call from <%r>\n",
			  &msg->from.auri);
	}
}

static bool sip_msg_handler(const struct sip_msg *msg, void *arg)
{
	re_printf("sip_msg_handler <%r>\n",
		  &msg->from.auri);


	return true;
}

/* called when all sip transactions are completed */
static void exit_handler(void *arg)
{
	(void)arg;

	re_printf("exit_handler \n");

	/* stop libre main loop */
	re_cancel();
}


/* called upon reception of SIGINT, SIGALRM or SIGTERM */
static void signal_handler(int sig)
{
	re_printf("terminating on signal %d...\n", sig);

	terminate();
}

int init()
{
	struct sa nsv[16];
	struct sa laddr;
	uint32_t nsc;
	int err; /* errno return values */

	/* enable coredumps to aid debugging */
	(void)sys_coredump_set(true);

	err = fd_setsize(1024);
	if (err) {
		re_fprintf(stderr, "fd_setsize error: %s\n", strerror(err));
		return err;
	}

	/* initialize libre state */
	err = libre_init();
	if (err) {
		re_fprintf(stderr, "re init failed: %s\n", strerror(err));
		return err;
	}

	nsc = ARRAY_SIZE(nsv);
	/* fetch list of DNS server IP addresses */
	err = dns_srv_get(NULL, 0, nsv, &nsc);
	if (err) {
		re_fprintf(stderr, "unable to get dns servers: %s\n",
			   strerror(err));
		return err;
	}

	/* create DNS client */
	err = dnsc_alloc(&dnsc, NULL, nsv, nsc);
	if (err) {
		re_fprintf(stderr, "unable to create dns client: %s\n",
			   strerror(err));
		return err;
	}

	/* create SIP stack instance */
	err = sip_alloc(&sip, dnsc, 32, 32, 32,
			"ua demo v" VERSION " (" ARCH "/" OS ")",
			exit_handler, NULL);
	if (err) {
		re_fprintf(stderr, "sip error: %s\n", strerror(err));
		return err;
	}

	/* fetch local IP address */
	err = net_default_source_addr_get(AF_INET, &laddr);
	if (err) {
		re_fprintf(stderr, "local address error: %s\n", strerror(err));
		return err;
	}

	/* listen on random port */
	sa_set_port(&laddr, 5060);

	/* add supported SIP transports */
	err |= sip_transp_add(sip, SIP_TRANSP_UDP, &laddr);
	err |= sip_transp_add(sip, SIP_TRANSP_TCP, &laddr);
	if (err) {
		re_fprintf(stderr, "transport error: %s\n", strerror(err));
		return err;
	}

	/* create SIP session socket */
	err = sipsess_listen(&sess_sock, sip, 32, connect_handler, NULL);
	if (err) {
		re_fprintf(stderr, "session listen error: %s\n",
			   strerror(err));
		return err;
	}

	err = sip_listen(&lsnr, sip, true, sip_msg_handler, NULL);
	if (err)
		return err;

	/* create SDP session */
	err = sdp_session_alloc(&sdp, &laddr);
	if (err) {
		re_fprintf(stderr, "sdp session error: %s\n", strerror(err));
		return err;
	}
//	sdp_session_set_lbandwidth(sdp, SDP_BANDWIDTH_AS, 1024);

	return 0;
}

int confaudio()
{
	int err;
	struct sa laddr;

	/* fetch local IP address */
	err = net_default_source_addr_get(AF_INET, &laddr);
	if (err) {
		re_fprintf(stderr, "local address error: %s\n", strerror(err));
		return err;
	}

	err = rtp_listen(&rtp1, IPPROTO_UDP, &laddr, 10000, 30000, true,
			 rtp_handler, rtcp_handler, &rtp1);
	err = rtp_listen(&rtp1_, IPPROTO_UDP, &laddr, 10000, 30000, true,
			 rtp_handler, rtcp_handler, &rtp1_);
	if (err) {
		re_fprintf(stderr, "rtp listen error: %m\n", err);
		return err;
	}
	re_printf("audio local RTP port is %u\n", sa_port(rtp_local(rtp1)));

	/* add audio sdp media, using port from RTP socket */
	err = sdp_media_add(&sdp_media1, sdp, "audio",
			    sa_port(rtp_local(rtp1)), "RTP/AVP");
	if (err) {
		re_fprintf(stderr, "sdp media error: %s\n", strerror(err));
		return err;
	}
	re_printf("audio sdp_media_add\n");

	err = sdp_format_add(NULL, sdp_media1, false, "0", "PCMU", 8000, 1,
			     NULL, NULL, NULL, false, NULL);
	if (err) {
		re_fprintf(stderr, "sdp format error: %s\n", strerror(err));
		return err;
	}
	re_printf("audio sdp_format_add\n");

	return 0;
}

int confvideo()
{
	int err;
	struct sa laddr;

	/* fetch local IP address */
	err = net_default_source_addr_get(AF_INET, &laddr);
	if (err) {
		re_fprintf(stderr, "local address error: %s\n", strerror(err));
		return err;
	}

	/* create the RTP/RTCP socket */
	err = rtp_listen(&rtp, IPPROTO_UDP, &laddr, 10000, 30000, true,
			 rtp_handler, rtcp_handler, &rtp);
	if (err) {
		re_fprintf(stderr, "rtp listen error: %m\n", err);
		return err;
	}
	re_printf("video rtp local RTP port is %u\n", sa_port(rtp_local(rtp)));

	/* add audio sdp media, using port from RTP socket */
	err = sdp_media_add(&sdp_media, sdp, "video",
			    sa_port(rtp_local(rtp)), "RTP/AVP");
	if (err) {
		re_fprintf(stderr, "sdp media error: %s\n", strerror(err));
		return err;
	}
	
	/* add G.711 sdp media format */
	err = sdp_format_add(NULL, sdp_media, false, NULL, "H264", 90000, 1,
			     NULL, NULL, NULL, false, NULL);
	if (err) {
		re_fprintf(stderr, "sdp format error: %s\n", strerror(err));
		return err;
	}
//////////////////////
	err = rtp_listen(&rtp_, IPPROTO_UDP, &laddr, 10000, 30000, true,
			 rtp_handler, rtcp_handler, &rtp_);
	if (err) {
		re_fprintf(stderr, "rtp listen error: %m\n", err);
		return err;
	}
	re_printf("video rtp_ local RTP port is %u\n", sa_port(rtp_local(rtp_)));
/*
	err = sdp_media_add(&sdp_media_, sdp, "video",
			    sa_port(rtp_local(rtp_)), "RTP/AVP");
	if (err) {
		re_fprintf(stderr, "sdp media error: %s\n", strerror(err));
		return err;
	}
	
	err = sdp_format_add(NULL, sdp_media_, false, NULL, "H264", 90000, 1,
			     NULL, NULL, NULL, false, "profile-level-id=64001f; packetization-mode=1; max-fs=8000");
	if (err) {
		re_fprintf(stderr, "sdp format error: %s\n", strerror(err));
		return err;
	}
*/
	
//	sdp_media_set_lbandwidth(sdp_media, SDP_BANDWIDTH_TIAS, 1024000);

//	sdp_media_set_lattr(sdp_media, false, "fmtp", "110 profile-level-id=42801e; packetization-mode=1; max-mbps=49000; max-br=20010; sar=13");

	return 0;
}

int main(int argc, char *argv[])
{
	int err;

	if( init() )
		goto out;

	if( confvideo() )
		goto out;

	if( confaudio() )
		goto out;

	/* main loop */
	err = re_main(signal_handler);

 out:
	/* clean up/free all state */
	mem_deref(sdp); /* will also free sdp_media */
	mem_deref(rtp);
	mem_deref(sess_sock);
	mem_deref(sip);
	mem_deref(dnsc);

	/* free libre state */
	libre_close();

	/* check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}
