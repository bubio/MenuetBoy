# AGENTS.md

This file provides guidance to AI coding agents when working with code in this repository.

## プロジェクト概要

[MenuetOS](https://www.menuetos.net/index.html) 上で動作する GAME BOY エミュレーター「MenuetBoy」。
要件メモは `docs/dev/README.md`。

## 要件・制約

- 対象は GAME BOY (DMG) のみ。CGB / SGB 対応はスコープ外
- BIOS (ブート ROM) を必要としない。起動直後のレジスタ・I/O 状態は BIOS 実行後の値を直接セットする前提
- MenuetOS は 64bit 版を対象とする
- MenuetOS は VM（UTM）上で動作させてテストする
- MenuetOS の技術資料（システムコール等）: https://www.menuetos.net/docs.htm

## 開発ドキュメント

開発関連の情報は `docs/dev/` に置く（新しく書く場合もここに作る）。

- `docs/dev/environment.md` — UTM VM の必須設定、fasm(blink) でのビルド、ISO を作り直して VM に渡す手順
- `docs/dev/menuetos-app.md` — MenuetOS 64bit アプリのヘッダーとシステムコール
- `docs/dev/apu.md` — サウンドの実装（遅延評価）と syscall 117 での再生

VM の画面はホストから撮れないため、VM 上の動作確認はユーザーに依頼する。

## ビルドとテスト

```sh
scripts/build.sh [rom]           # MenuetOS アプリをビルドし、VM の CD イメージを作り直す（要 brew install blink xorriso）
                                 # rom は ROM.GB として既定で起動、roms/ のファイルはそのままの名前で同じフォルダーに入る
scripts/test.sh                  # コアを Mac 上でテスト（Blargg・Mooneye のテスト ROM、dmg-acid2/halt_bug 画面比較、440Hz トーン、MBC3 RTC）
scripts/test.sh tests/roms/02-interrupts.gb   # 1 本だけ実行
```

`scripts/test.sh` は `tests/harness.asm`（Linux x86-64 ELF）を blink で実行する。ROM のシリアル出力（dmg_sound はカートリッジ RAM の結果）を表示し、`Passed` / `Failed`（Mooneye は Fibonacci のバイト列 / 0x42）で判定する。画面にしか結果を出さない ROM は `tests/screen/` に置き、`tests/ref/` の PNG と比較する。
`build/harness <rom> <frames> <out.pgm> [out.raw]` で LCD を PGM に、音声を s16le ステレオ 48kHz で書き出せる（blink 経由で実行、パスは `/SystemRoot` 付きの絶対パス）。
MenuetOS 上での動作確認は VM を再起動して CD の `/MENUETBOY/MENUETBOY` を起動し、ユーザーに画面を見てもらう。

## コード構成

エミュレーターのコア（`src/core/`）はシステムコールを使わず、MenuetOS 版とテストハーネスの両方から include する。

- `src/core/gb.inc` — 入口（`gb_reset` / `gb_run_frame` / `gb_set_button`）、タイマー、PPU のタイミングと STAT 割り込み
- `src/core/cpu.inc` — SM83 インタプリタ。オペコード表 `op_table` から汎用ハンドラーへ分岐し、レジスタ番号はオペコードのビットから取り出す。メモリアクセスのたびに `cpu_read` / `cpu_write` / `cpu_idle` が 4 T サイクル分 `gb_tick` を進める（M サイクル単位のタイミング）
- `src/core/ppu.inc` — HBlank に入るたびに 1 ライン描画（BG・ウィンドウ・オブジェクト）して `gb_lcd`（濃淡 0-3）に書く
- `src/core/mmu.inc` — メモリマップ、MBC1/2/3/5、I/O レジスタ、OAM DMA の開始（転送は `gb_tick` で 1 M サイクル 1 バイト）。MBC3 の RTC は `gb.inc` の `rtc_tick` がエミュレーター内の時間で進める（実時間・終了中の経過は反映しない）
- `src/core/apu.inc` — サウンド。`gb_tick` はサイクルを溜めるだけで、レジスタアクセス時などに `apu_sync` で追いつき、48kHz ステレオを `apu_ring` に書く
- `src/core/state.inc` — 全状態を `virtual at GB_STATE` で定義。フロントエンドが `GB_STATE`・`gb_rom`・`platform_serial` を用意する
- `src/menuetboy.asm` — MenuetOS フロントエンド。先頭にアプリ内メモリマップ。補正した時計で 59.7fps に合わせて `gb_run_frame` を呼び、`gb_lcd` を 2 倍に拡大して syscall 7 で表示し、`apu_ring` を syscall 117 のダブルバッファに流す
- ROM は起動パラメータのパス、なければ `/CD/1/MENUETBOY/ROM.GB`（VM で確認済み）から読み込む
- 電池付きカートリッジの RAM は `/FD/1/<ROM 名>.SAV` に保存する。`/FD/1` はメモリ上の写しなので MenuetOS を再起動すると消える（ディスクに残せる保存先は VM 上にない。`docs/dev/environment.md` のセーブ）。RAM への書き込みが 60 フレーム止まったときと終了時に書く。確認用 ROM は `tests/save_counter.py`
- フレームの時間合わせは、TCG で遅れる ms タイマーを RTC の秒で補正した時計で行う（`get_time_us`）

フラグ計算は x86 の `lahf` 結果を `flag_table` で GB の Z/H/C に変換している。ただし blink は `inc` で AF を立てない（確認済み、`dec` は未確認）ため、INC/DEC の H フラグは結果の値から直接計算する（`lahf` に頼るとテストだけが落ちる）。
