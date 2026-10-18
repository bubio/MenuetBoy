# MenuetBoy

[MenuetOS](https://www.menuetos.net/index.html)用のGAME BOYエミュレーターの開発です。

- GAME BOY (DMG)のみを対象とします
- BIOSを必要としません
- MenuetOSは64bitを対象とします
- MenuetOSはVM（UTM）で動作させます
- MenuetOSの技術資料は　https://www.menuetos.net/docs.htm にあります

## 開発ドキュメント

- [environment.md](environment.md) — UTM VM・ビルドツール・VM へのファイル受け渡し
- [menuetos-app.md](menuetos-app.md) — MenuetOS 64bit アプリの形式と、エミュレーターで使うシステムコール
- [apu.md](apu.md) — サウンドの実装と MenuetOS での再生
