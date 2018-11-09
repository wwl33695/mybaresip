/**
 * @file src/main.c  Main application code
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#define __ssize_t_defined

#ifdef SOLARIS
#define __EXTENSIONS__ 1
#endif
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT
#include <getopt.h>
#endif
#include <re/re.h>
#include <baresip.h>

static struct rtp_sock *rtp, *rtp1, *rtp_, *rtp1_;            /* RTP socket         */

static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		mod_close();
		exit(0);
	}

	term = true;

	info("terminated by signal %d\n", sig);

	ua_stop_all(false);
}


static void ua_exit_handler(void *arg)
{
	(void)arg;
	debug("ua exited -- stopping main runloop\n");

	/* The main run-loop can be stopped now */
	re_cancel();
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

int confaudio(struct sdp_session *sdp)
{
	int err;
	struct sa laddr;
	struct sdp_media *sdp_media1;     /* SDP media          */

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

int confvideo(struct sdp_session *sdp)
{
	int err;
	struct sa laddr;
	struct sdp_media *sdp_media, *sdp_media_;     /* SDP media          */

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

static void usage(void)
{
	(void)re_fprintf(stderr,
			 "Usage: baresip [options]\n"
			 "options:\n"
#if HAVE_INET6
			 "\t-6               Prefer IPv6\n"
#endif
			 "\t-d               Daemon\n"
			 "\t-e <commands>    Execute commands (repeat)\n"
			 "\t-f <path>        Config path\n"
			 "\t-m <module>      Pre-load modules (repeat)\n"
			 "\t-p <path>        Audio files\n"
			 "\t-h -?            Help\n"
			 "\t-t               Test and exit\n"
			 "\t-n <net_if>      Specify network interface\n"
			 "\t-u <parameters>  Extra UA parameters\n"
			 "\t-v               Verbose debug\n"
			 );
}

static void myua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct player *player = baresip_player();

	(void)call;
	(void)prm;
	(void)arg;

	printf("myua_event_handler: [ ua=%s call=%s ] event: %s (%s)\n",
	      ua_aor(ua), call_id(call), uag_event_str(ev), prm);

	switch (ev) 
	{
        case UA_EVENT_REGISTERING:
            break;
        case UA_EVENT_REGISTER_OK:
            break;
        case UA_EVENT_REGISTER_FAIL:
            break;
        case UA_EVENT_UNREGISTERING:
            break;
        case UA_EVENT_SHUTDOWN:
            break;
        case UA_EVENT_EXIT:
            break;
        case UA_EVENT_CALL_INCOMING:

//            sdp_session_set_lbandwidth(call_sdp(call), SDP_BANDWIDTH_AS, 1024);

            ua_answer(ua, call);
            break;
        case UA_EVENT_CALL_RINGING:
            break;
        case UA_EVENT_CALL_ESTABLISHED:
            break;
        case UA_EVENT_CALL_CLOSED:
            break;
        case UA_EVENT_CALL_TRANSFER_FAILED:
            break;
        default:
            break;
	}
}

static bool sub_handler(const struct sip_msg *msg, void *arg)
{
	struct ua *ua;

	(void)arg;

	return true;
}

int main(int argc, char *argv[])
{
	bool prefer_ipv6 = false, run_daemon = false, test = false;
	const char *ua_eprm = NULL;
	const char *execmdv[16];
	const char *net_interface = NULL;
	const char *audio_path = NULL;
	const char *modv[16];
	size_t execmdc = 0;
	size_t modc = 0;
	size_t i;
	int err;
	
	log_enable_debug(true);
	log_enable_info(true);
	log_enable_stdout(true);
	uag_event_register(myua_event_handler, NULL);
	/*
	 * turn off buffering on stdout
	 */
	setbuf(stdout, NULL);

	(void)re_fprintf(stdout, "baresip v%s"
			 " Copyright (C) 2010 - 2018"
			 " Alfred E. Heggestad et al.\n",
			 BARESIP_VERSION);

	(void)sys_coredump_set(true);

	err = fd_setsize(1024);
	if (err) {
		warning("fd_setsize error: %s\n", strerror(err));
		goto out;
	}

	err = libre_init();
	if (err)
		goto out;

#ifdef HAVE_GETOPT
	for (;;) {
		const int c = getopt(argc, argv, "6de:f:p:hun:vtm:");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		case 'h':
			usage();
			return -2;

#if HAVE_INET6
		case '6':
			prefer_ipv6 = true;
			break;
#endif

		case 'd':
			run_daemon = true;
			break;

		case 'e':
			if (execmdc >= ARRAY_SIZE(execmdv)) {
				warning("max %zu commands\n",
					ARRAY_SIZE(execmdv));
				err = EINVAL;
				goto out;
			}
			execmdv[execmdc++] = optarg;
			break;

		case 'f':
			conf_path_set(optarg);
			break;

		case 'm':
			if (modc >= ARRAY_SIZE(modv)) {
				warning("max %zu modules\n",
					ARRAY_SIZE(modv));
				err = EINVAL;
				goto out;
			}
			modv[modc++] = optarg;
			break;

		case 'p':
			audio_path = optarg;
			break;

		case 't':
			test = true;
			break;

		case 'n':
			net_interface = optarg;
			break;

		case 'u':
			ua_eprm = optarg;
			break;

		case 'v':
			log_enable_debug(true);
			break;

		default:
			break;
		}
	}
#else
	(void)argc;
	(void)argv;
#endif
	
	conf_path_set("./conf");
	err = conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	/*
	 * Set the network interface before initializing the config
	 */
	if (net_interface) {
		struct config *theconf = conf_config();

		str_ncpy(theconf->net.ifname, net_interface,
			 sizeof(theconf->net.ifname));
	}

	/*
	* Initialise the top-level baresip struct, must be
	* done AFTER configuration is complete.
	*/
	err = baresip_init(conf_config(), prefer_ipv6);
	if (err) {
		warning("main: baresip init failed (%m)\n", err);
		goto out;
	}

	struct player *play = baresip_player();
	play_set_path(play, "./share");
	
	/* Set audio path preferring the one given in -p argument (if any) */
	if (audio_path)
		play_set_path(baresip_player(), audio_path);
	else if (str_isset(conf_config()->audio.audio_path)) {
		play_set_path(baresip_player(),
			      conf_config()->audio.audio_path);
	}

	/* NOTE: must be done after all arguments are processed */
	if (modc) {

		info("pre-loading modules: %zu\n", modc);

		for (i=0; i<modc; i++) {

			err = module_preload(modv[i]);
			if (err) {
				re_fprintf(stderr,
					   "could not pre-load module"
					   " '%s' (%m)\n", modv[i], err);
			}
		}
	}

	/* Initialise User Agents */
	err = ua_init("baresip v" BARESIP_VERSION " (" ARCH "/" OS ")",
		      true, true, true, prefer_ipv6);
	if (err)
		goto out;

	struct ua *uap = NULL;
	ua_alloc(&uap, "sip:user@192.168.0.101");
	uag_set_exit_handler(ua_exit_handler, NULL);

	if (ua_eprm) {
		err = uag_set_extra_params(ua_eprm);
		if (err)
			goto out;
	}

	if (test)
		goto out;

	/* Load modules */
	err = conf_modules();
	if (err)
		goto out;

	if (run_daemon) {
		err = sys_daemon();
		if (err)
			goto out;

		log_enable_stdout(false);
	}

	info("baresip is ready.\n");

	/* Execute any commands from input arguments */
	for (i=0; i<execmdc; i++) {
		ui_input_str(execmdv[i]);
	}

	/* Main loop */
	err = re_main(signal_handler);

 out:
	if (err)
		ua_stop_all(true);

	ua_close();
	conf_close();

	baresip_close();

	/* NOTE: modules must be unloaded after all application
	 *       activity has stopped.
	 */
	debug("main: unloading modules..\n");
	mod_close();

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}
