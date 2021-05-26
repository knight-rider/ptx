[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=MX5ZFYH5XH7GS)

DVB driver for Earthsoft PT3, PLEX PX-Q3PE ISDB-S/T PCIE cards & PX-BCUD ISDB-S USB dongle
==========================================================================================

Status: stable

Features:
1. in addition to the real frequency:
	ISDB-S : freq. channel ID
	ISDB-T : freq# (I/O# +128), ch#, ch# +64 for CATV
2. in addition to TSID:
	ISDB-S : slot#

Supported Cards & Main components:
A. EarthSoft PT3:
1. Altera	EP4CGX15BF14C8N	: customized FPGA PCI bridge
2. Toshiba	TC90522XBG	: quad demodulator (2ch OFDM + 2ch 8PSK)
3. Sharp	VA4M6JC2103	: contains 2 ISDB-S + 2 ISDB-T tuners
	ISDB-S : Sharp QM1D1C0042 RF-IC, chip ver. 0x48
	ISDB-T : MaxLinear CMOS Hybrid TV MxL301RF

B. PLEX PX-Q3PE:
1. ASICEN	ASV5220		: PCI-E bridge
2. Toshiba	TC90522XBG	: quad demodulator (2ch OFDM + 2ch 8PSK)
3. NXP Semiconductors TDA20142	: ISDB-S tuner
4. Newport Media NM120		: ISDB-T tuner
5. ASICEN	ASIE5606X8	: crypting controller

C. PLEX PX-BCUD (ISDB-S USB dongle)
1. Empia	EM28178		: USB I/F (courtesy of 長浜様)
2. Toshiba	TC90532		: demodulator (using TC90522 driver)
3. Sharp	QM1D1C0045_2	: ISDB-S RF-IC, chip ver. 0x68

Notes:
This is a complex but smartly polished driver package containing 2 (dual head)
PCI-E bridge I/F drivers, single demodulator frontend, and 4 (quad tail) tuner drivers,
plus, simplified Nagahama's patch for PLEX PX-BCUD (ISDB-S USB dongle).
Generic registration related procedures (subdevices, frontend, etc.) summarized in
ptx_common.c are very useful also for other DVB drivers, and would be very handy if
inserted into the core (e.g. dvb_frontend.c & dvb_frontend.h).

For example, currently, the entity of struct dvb_frontend is created sometimes in
demodulators, some in tuners, or even in the parent (bridge) drivers. IMHO, this entity
should be provided by dvb_core. ptx_register_fe() included in ptx_common.c simplifies
the tasks and in fact, significantly reduces coding & kernel size.

Also, currently dvb_frontend's .demodulator_priv & .tuner_priv are of type (void *).
These should be changed to (struct i2c_client *), IMHO. Private data for demodulator
or tuner should be attached under i2c_client, using i2c_set_clientdata() for instance.

FILENAME	& SUPPORTED CHIPS:
- tc90522.c	: TC90522XBG, TC90532XBG,...
- tda2014x.c	: TDA20142
- qm1d1c004x.c	: QM1D1C0042, QM1D1C0045, QM1D1C0045_2
- nm131.c	: NM131, NM130, NM120
- mxl301rf.c	: MxL301RF
- pt3_pci.c	: EP4CGX15BF14C8N
- pxq3pe_pci.c	: ASV5220

Full package:
- URL:	https://github.com/knight-rider/ptx
- buildable as standalone, DKMS or tree embedded module
- インストール方法:
	DKMS がなければ
	# make install
	DKMS があれば（自動アップデート）
	$ chmod +x dkms.install dkms.uninstall
	$ ./dkms.install

PTX 用ツール集:
- apps   	: アプリ集
- drivers	: PX-Q3PE と PT3 ドライバ（cdev 版と DVB 版）、
		  PX-BCUD DVB ドライバ 長浜版（改）

PX-Q3PE は、ctrl-alt-del 等の reboot（warm boot）ではリセットされません。
（ドライバで出来るかもしれないが方法が分かりません。誰か教えて下さい）
必ず電源を切って完全に落してから（再）起動して下さい。

PX-BCUD ドライバは未検証です。他の色々なドライバに依存しているため、動かない場合があります。
その場合、このツリーのソースに習ってご自身のカーネルを pxbcud.patch で修正した方が良い
でしょう。動く筈です。

最上位 Makefile は通常、弄らない。dkms.conf 或いは下位 Makefile を編集して下さい。

今まで PLEX 社等からは一切支援や援助を受けていませんが、検証用の機材を提供して頂ければ
他機種のドライバも作ってあげられなくもなくなくない、かも知れません。

ご質問はメールでどうぞ...