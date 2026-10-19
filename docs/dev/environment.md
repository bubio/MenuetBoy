# 開発環境

**結論**: Mac (Apple Silicon) 上で fasm の Linux x86-64 版を blink で動かしてビルドし、成果物を公式 ISO に追加した ISO を xorriso で作り直して UTM VM の CD から読ませる。
fasm にネイティブ arm64 版がなく、ISO 追記は公式 ISO の起動構成に手を加えずに済むため。
VM 側では CD の `/MENUETBOY/MENUETBOY` を起動する。

## MenuetOS の入手

Menuet64 は再頒布禁止のライセンスのため（[m64l.txt](https://www.menuetos.net/m64l.txt)、個人・教育目的は無償）、ISO はリポジトリに含めず各自が公式サイトから入手する。
ビルドで作る `MenuetBoy.ISO` も OS 本体を含むので配布しない。

1. https://www.menuetos.net/download.htm の「M64 - 80 MB Bootable CD image」（`M64CD-1.61.00.ISO`、Google Drive）をダウンロードする
2. `DevContainer/MenuetOS.utm/M64CD16100.ISO` として置く（ビルドのベース。手元の版の SHA-256 は `186af6eafca1904312194137f48ca67a38071574534457c8dff1e17de4a9fa60`）
3. VM が参照する CD のリンクを作る: `mkdir -p DevContainer/MenuetOS.utm/Data && ln -s ../MenuetBoy.ISO DevContainer/MenuetOS.utm/Data/MenuetBoy.ISO`

## ビルド

```sh
brew install blink xorriso   # 初回のみ。fasm は scripts/build.sh が .tools/ に自動取得する
scripts/build.sh [rom]       # 既定の ROM は roms/dmg-acid2.gb
```

`build/MENUETBOY` をビルドし、`DevContainer/MenuetOS.utm/MenuetBoy.ISO` を作り直す。ISO の `/MENUETBOY/` には次が入る:

| ファイル | 内容 |
|---|---|
| `MENUETBOY` | アプリ本体 |
| `ROM.GB` | 引数で指定した ROM。アプリが起動パラメータなしで読み込む |
| `roms/` の各ファイル | 同じファイル名で収録（ISO9660 レベル2 なので 31 文字まで）。名前は置く人が整える。`roms/` は `dmg-acid2.gb` 以外 git の対象外 |

起動フロッピーの `config.mnt` は、音声を AC97 ドライバーで有効にし（[apu.md](apu.md)）、CD `cd_1`（プライマリのマスター、`cd_1_base = 0x1f0`）を有効にするよう書き換えて収録する。CD も明示するので、起動時の選択肢 3（自動設定なし）でも `/CD/1` が見える。キーボード配列も英語（`keyboard_layout = 2` の eng。既定の 0 では `/` が `-` になる）にする。

ISO は別名で書き出してから置き換えるので、VM の起動中でも実行できる。反映は VM の再起動後。

## UTM VM

VM 本体は `DevContainer/MenuetOS.utm`（UTM に登録済み）。ホストは Apple Silicon なので x86_64 をエミュレーション（TCG）で動かす。

起動に必須の設定（`config.plist`）:

| 項目 | 値 | 外れたときの症状 |
|---|---|---|
| `System.Architecture` | `x86_64` | `i386` だと `64bit CPU unavailable.` で停止 |
| `System.Target` | `pc`（i440FX） | `q35` + PS/2 なしだとテキスト表示後に停止 |
| `Display.0.Hardware` | `VGA` | virtio 系ではデスクトップに遷移しない |
| `QEMU.UEFIBoot` | `false` | MenuetOS は BIOS 起動 |
| `Sound.0.Hardware` | `AC97` | 音が出ない（`config.mnt` のドライバーと不一致になる） |

CD イメージの配置:

| ファイル | 役割 |
|---|---|
| `M64CD16100.ISO` | 公式配布 ISO（v1.61.00）。ビルドのベースで、変更しない |
| `MenuetBoy.ISO` | ビルドで生成する ISO（公式 ISO + `/MENUETBOY/`） |
| `Data/MenuetBoy.ISO` | VM が参照する CD。`../MenuetBoy.ISO` へのシンボリックリンク |

UTM はイメージを `Data/` 相対でしか参照できないため、ビルドの出力をシンボリックリンクで参照する。

- 操作: `/Applications/UTM.app/Contents/MacOS/utmctl {list|start|stop|status} MenuetOS`
- `config.plist` を直接編集するときは UTM を終了してから行う（起動中の UTM はメモリ上の設定で上書きする）
- ホスト端末に画面収録権限がなく `screencapture` は失敗する。VM の画面確認はユーザーに依頼する

## ビルドツール

fasm 1.73.35 の Linux 版（`fasm-1.73.35.tgz` 内の `fasm.x64`、静的リンクの ELF64）を blink で実行する。blink の注意点:

- `BLINK_PREFIX`: 未指定だと blink が `/` にマウントポイントを作ろうとして失敗する。ホストのファイルは `/SystemRoot/...` で見える
- `-m`: fasm.x64 は blink の linear memory 最適化と合わないため必須
- カレントディレクトリからの相対パスは通常どおり使える
- `inc` 命令で AF（補助キャリー）が立たない（`add` では立つ）。実機の x86 とは異なるため、コアのフラグ計算は INC/DEC で AF に頼らない

| 却下した案 | 却下理由 |
|---|---|
| fasm / fasmg の macOS 版 | i386 Mach-O のみで、現行 macOS では実行不可（Rosetta 2 も 32bit 非対応） |
| NASM（arm64 ネイティブ） | MenuetOS のサンプル・資料が fasm 構文で、移植の手間が増える |
| VM 内の FASM でビルド | Mac のエディタ・git と分断され、毎回 VM 内で操作が必要 |
| Linux VM / Docker で fasm | 追加の VM や常駐プロセスが必要。blink は単体の 1.3MB で済む |

## VM へのファイル受け渡し（ISO 作り直し）

公式 ISO は El Torito のフロッピーエミュレーション（1.44MB、FAT12、ラベル `MENUET 64`）で起動し、ブートイメージはファイルツリーに見えない形で LBA 25 に置かれている。
xorriso はこの隠しイメージを引き継げないため、取り出して明示的に指定し直す。
公式 ISO は Rock Ridge / Joliet なしの ISO9660（小文字のファイル名あり）なので、同じ形式で書き出す。

手順の実体は `scripts/build.sh`。ディレクトリ名 `MENUETBOY` が 9 文字のため ISO9660 レベル2 で書き出す（レベル1 だと 8 文字に切り詰められる）。
この方式で作った ISO での起動、`/MENUETBOY/`（9 文字、レベル2 の名前）の表示、サンプルアプリのウィンドウ表示まで VM 上で確認済み。

| 却下した案 | 却下理由 |
|---|---|
| ブートフロッピー（FAT12）に mtools で追加 | 1.44MB で空きが少なく、ROM を置く余地がない |
| FAT32 の HDD イメージに ROM を置く | ISO に入れれば足りる |
| ネットワーク（FTP/HTTP）で転送 | VM 側の起動のたびに手作業の転送操作が必要 |

## セーブ

**結論**: セーブは `/FD/1/<ROM 名>.SAV` に書く。`/FD/1` は起動時にメモリへ読み込んだ起動フロッピーの写しなので、MenuetOS を再起動すると消える。
VM 上でディスクに残せる保存先は見つからなかった（下表）。MenuetBoy の読み書き自体は `/FD/1` で確認済み。

| 試した保存先 | 結果 |
|---|---|
| `/RD/1` | error 0C（読み込み専用） |
| IDE の HDD `/HD/1`（FAT32） | 読めるが書けない（error 0A）。MenuetOS 64bit は v0.09 から IDE の FAT32 を読み込み専用で扱い、QEMU のトレースでも書き込みコマンドが出ない |
| USB メモリ `/USB/1`（EHCI を QEMU 引数で追加） | SeaBIOS が起動時に列挙済みで、MenuetOS がポートをリセットせず認識しない（`usb_legacy_disable` も効かない） |
| フロッピーから起動し `fd_1_write` / CACHE2FD | `/FD/1` への保存は成功するが、イメージファイルに書き込まれない（原因は未調査） |

調査には `config.plist` の `QEMU.DebugLog` と `QEMU.AdditionalArguments` の `-trace`（`ide_*`、`usb_ehci_*`、`scsi_*` など）が使えた。ログは `Data/debug.log` に出る。

やらないこと: 上記の原因の追究（MenuetOS と UTM/QEMU の組み合わせの問題で、MenuetBoy 側では直せない）。

## VM 上の注意

- アプリからは `/CD/1/MENUETBOY/ROM.GB` で読める（確認済み）。読み込みは 1 ブロック読んでサイズを得てから必要なブロック数だけ読む（8MB を一度に要求すると error 05 になった）
- TCG ではシステムの ms タイマー（syscall 26/11）が実時間より遅く進む（実測 786 ms/秒）。RTC（syscall 3）の秒と音声の再生速度は正しい。時間計測は RTC の秒で補正する

## やらないこと

- VM 内での開発環境（エディタ・FASM）の整備
