# 開発環境

**結論**: Mac (Apple Silicon) 上で fasm の Linux x86-64 版を blink で動かしてビルドし、成果物を公式 ISO に追加した ISO を xorriso で作り直して UTM VM の CD から読ませる。
fasm にネイティブ arm64 版がなく、ISO 追記は公式 ISO の起動構成に手を加えずに済むため。
VM 側では CD の `/MENUETBOY/MENUETBOY` を起動する。

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
| `roms/` の各ファイル | 大文字にしたファイル名で収録 |

起動フロッピーの `config.mnt` は、音声を AC97 ドライバーで有効にするよう書き換えて収録する（[apu.md](apu.md)）。

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
| `Data/M64CD16100.ISO` | VM が参照する CD。上のどちらかへのシンボリックリンク（現在は `MenuetBoy.ISO`） |

UTM はイメージを `Data/` 相対でしか参照できないため、シンボリックリンクで切り替える。

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
| FAT32 の HDD イメージを追加 | MenuetOS 側で `config.mnt` の IDE 設定が必要。qcow2 はホストから直接書けず、raw イメージへの置き換えも要る |
| ネットワーク（FTP/HTTP）で転送 | VM 側の起動のたびに手作業の転送操作が必要 |

## 未検証

- ファイルブラウザーでは CD が `/CD/1/MENUETBOY` と表示される。syscall 58 で `/cd/1/MENUETBOY/ROM.GB`（小文字のデバイス名）は error 05 だった。大文字の `/CD/1/...` で読めるかは未確認

## やらないこと

- VM 内での開発環境（エディタ・FASM）の整備
