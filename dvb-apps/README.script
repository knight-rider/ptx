構成

scripts/gst0.10/*
 録画用スクリプト とヘルパースクリプト等
 録画にはgstreamer 0.10 のISDB用パッチ当て版を用いる。

scripts/gst1.2/*
 上記と同じスクリプトをgstreamer 1.2系に対応させたもの。
 各スクリプトの使い方は、（末尾の3を除いた）同名の0.10系用バージョンと同じ。

---------------
各スクリプトの概要

dvb_sched/dvb_sched3
  at(1)を使って時間予約録画をする. dvb_sched -h でヘルプ

  dvb_sched <HH:MM> <YYYY-mm-dd> -c <CHANNEL-NAME> -l <LENGTH(min)> -o <OUTFILE>
  CHANNEL-NAMEは ~/.gstreamer-0.10/dvb-channel.confに記載されている名前
   日付,時間は録画の開始時間,LENGTHは録画時間を分で指定
  内部では指定時間1分前になるとdvbrecを起動し 指定された時間録画する.
  アダプタ番号やFE番号なども指定できる. 詳しくはdvbrecの説明を参照.

 ex. 2009年12月31日19時00分00秒から1時間, NHKを~/foo.tsに録画を予約する.
   dvb_sched 19:00 2009-12-31 -a 1 -c NHK -l 60 -o ~/foo.ts


 注意:
  atを使える環境になっていることが必要.
   また、ディストリによってはDVBデバイスの所有者を、
  一時的に現在のコンソール使用者に書き換えるようになっているので、
  ログアウトするとDVBデバイスの所有者がrootに戻されてしまいアクセスできなくなる。
   これを防いで、ログアウトしていてもタイマー録画できるようにしておくためには
     # echo '<console> 0660 <dvb> 0660 root.video' \
     #              > /etc/security/console.perms.d/90-dvb.perms
  のようにし(Fedoraの場合)、dvb_schedを実行するユーザをvideoグループに加れば良い。

   また、スクリプト内にヘルパースクリプトdvbrecへのパスがハードコードされているので
  各自の環境に応じて修正(先頭付近の DVBREC=... の行)が必要.

  gstreamerのISDBパッチ当て版をシステムインストールせず(un-installed)に用いている場合、
  先頭付近の UNINST_CMD=を正しく設定する必要がある。

------------

dvbrec.py/dvbrec3.py
  指定したチャネルで録画を開始し,指定された時間経過したら停止する.
  dvb_schedのヘルパースクリプトとしても使用される。 dvbrec.py -hでヘルプ
  (NOTE: バージョン<0.96から引数のフォーマット・意味が変わったので注意)

  -tで開始時刻を指定すると,その時間まで待機してから録画開始する.
  -oで出力ファイルを指定しないとstdoutへ出力する. エラーや報告はstderrへ出力される.
  ex. dvbrec.py -c NHK -l 120 > foo.ts    今から2時間NHKを録画する.

  アダプタ番号やFE番号などは, オプションで指定しなければすべて0が使われる.(空きを探したりしない)
  gstreamerのDVBプラグインを使うので, 環境変数GST_DVB_CHANNELS_CONFで指定したファイルか,
   デフォルトの設定ファイル ~/.gstreamer-0.10/dvb-channels.confまたは
  ~/.config/gstreamer-1.0/dvb-channels.conf にチャンネルリストが設定されている必要がある.

-------------

dvb_sched_ev2/dvb_sched_ev3
  番組(イベント)単位の予約録画。 開始時間を指定し;) 指定したチャネルの番組を予約録画する.
  dvb_sched_ev2 -hでヘルプ. 
  事前に当該番組のEIDを取得して それを追跡するので
  より正確に延長・繰り下げに対応できる(と期待)
  最大延長待ち時間は-w N（分)で指定する。 defaultは60

NOTE: 旧dvb_sched_evはdvb_sched_ev2へ統合し消去.
      dvb_sched_ev2 -n でEID事前取得しないのでdvb_sched_ev相当

NOTE: バージョン>=0.96から -k オプションで予約が重なった場合の挙動を選択できるようになった.
 -k all で 関連する/dev/dvb/adapterX/frontendYを既に使用している全プロセスのkillを試みる
 -k none は何のkillもおこなわない
 -k "regexp" で/dev/dvb/adapterX/frontendYを使用するプロセスの内, コマンド名(/proc/<PID>/cmdlist)が
    egrep -e 'regexp" にマッチするプロセスに限定して killを試みる
  デフォルトはall

 ex. 19時からのNHKの番組をfoo.tsに録画予約(既に本日19:00を超えていたら明日の19:00になる)
    dvb_sched_ev2 19:00 -c NHK -o ~/foo.ts

  開始時刻はat(1)にそのまま渡されるので atの理解する時刻であれば
  dvb_schedのような固定した形式でなくても一応動作する. 
   (例えば"1pm + 3days"など. at(1) のman 参照)
 このような特殊な(= 時刻+年月日形式でない)指定の場合は
 ダブルクォートで囲むなどして,１つの引数として渡すこと.
 ただし"時刻" "年月日"の形式の場合は2つの引数として渡してOK.
  ex.  dvb_sched_ev2 19:00 2010-04-01 ....
  
 時刻指定の後の引数はそのままdvbevrec2.pyに渡される. dvbevrec2.pyの説明を参照.

  注意: dvb_schedの注意と同様.
  + 予定開始時刻の1分前になると, 以前に起動して未終了のdvbevrecを強制的にkillするので注意.

---------------

dvbevrec2.py/dvbevrec3.py
  指定した/現番組(イベント)の録画. dvb_sched_ev2のヘルパースクリプトとしても使用される。
  dvbevrec2.py -hでヘルプ

 ex1. サービスID(プログラムID):3080のイベントID:1234で指定される番組を~/foo.tsに録画
    dvbevrec2.py -s 3080 -e 1234 > ~/foo.ts

 ex2. サービスIDの代わりにチャンネル名で指定
    dvbevrec2.py -c NHK -e 1234 > ~/foo.ts

 ex3. 現在放送中(か2分以内に開始する)番組を録画
    dvbevrec2.py -c NHK -o ~/foo.ts

 ex4. サービスID:3080のチャンネルの番組を3分間録画 アダプタ1を使用
    dvbevrec2.py -a 1 -s 3080 -l 3 -o ~/foo.ts

  イベントIDはEPGで収集したEIT情報から得られる. -eでイベントIDを指定しなければ
  現在放送中か 2分以内に開始する番組を録画する.

  EITをトラックしているので 録画時間は自動的に番組終了までとなり,
  野球延長などの場合は  (-eで指定していれば) 延長後の開始時刻になるまで待機する.
  また放送時間の延長にも対応し自動的に録画終了時間をずらす. 
  臨時ニュースなどの割り込みによる一時中断にも対応する(未テスト)

   イベントIDはサービスID毎に付けられているので -cか-sの指定は必須.
  サービスIDは channels.confにも記載されいてるので 代わりにチャンネル名も使用できる.
  -cと-sを両方してもOKだが チャンネル設定ファイルでの記載と矛盾するとアボートする.

  -lは分単位での指定なので注意. 番組の終了が先になれば指定時間未満でも終了する.
  前番組延長も含めて 録画開始までの最大待機時間は-wで指定. デフォルトは60分でアボート.

--------------

scripts/tsdump
  TSパケットのHEXダンプを表示する.デバッグ用
  ex. tsdump < foo.ts | lv

-------------

scripts/dropchk
  ../cmds/build/dumptsの出力をチェックし,パケット落ちを表示する
  ex. dumpts < foo.ts | grep '^0100' | dropchk | grep '-------' | wc
  パケットのCCカウンタの飛びがあれば ---------- を出力する.

