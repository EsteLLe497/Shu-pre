# 夏の田舎町（仮）

ソリューションを x64 でビルドし、ルートの `StartRuralMap.cmd` を開きます。無人駅の近くから散策できます。F2 でシーンエディターを開き、配置を調整できます。エディターの「配置を保存」はこのフォルダーの `scene-layout.txt` に書き込みます。

駅前の踏切を渡ると、坂の途中に民家が並びます。西へ曲がると農具小屋を経て12枚の水田を一周でき、東の石畳は神社に続きます。メインの道は坂の頂上を越え、南の木橋と東屋まで下ります。

神社へは森の中の参道から入れます。鳥居をくぐると手水舎があり、境内の奥では石段を上って拝殿の賽銭箱まで近づけます。

参道の脇と拝殿の裏には竹藪があります。川沿いは草の土手、礫の浅瀬、石積みが交互に現れ、駅前や分かれ道には案内板、自販機、カーブミラーなどがあります。外周の山並みと斜面を覆う森が、夏見の里を囲みます。

水田の水面は土の畦で囲った地形に沿います。F2 のウォーターブラシで水位を調整でき、稲やあぜ道はシーン上のオブジェクトと地形ブラシで編集できます。

F2 の「植生ブラシ」で道端の草、シダ、低木、野花、川辺の草を選び、半径と密度を調整して地面をドラッグします。消しゴムは選択中の種類だけ、または全種類に使えます。「配置を保存」で植生もマップに保存されます。

F2 の「環境音」で青い円を確認し、地面をクリックしてゾーンを追加できます。中心、半径、最大音量と WAV を設定し、「配置を保存」でこのマップに保存します。試遊では田んぼに近づくとカエル、川に近づくと水音が聞こえます。

木橋や川沿いの浅場では小魚の群れが泳ぎます。水田のあぜにはカエルがいて、ときどき跳ねます。水田や川岸にはトンボ、参道や駅前にはチョウが飛んでいます。

空には風で流れる雲があり、時刻に合わせて太陽や月が動きます。散策は 09:30 に始まり、ゲーム内の一日は既定で現実の 12 分です。エディターの「空と時間」で時刻や一日の長さを変更できます。F2 の試遊は選んだ時刻から始まり、編集に戻ると試遊前の時刻に戻ります。

## 素材の出典

- `station.glb` は Grab3D の Train Station（CC0 1.0）。駅舎と屋根の色を調整しました。[配布ページ](https://grab3d.com/models/buildings/train-station/)
- `railway.glb` は Grab3D の Straight Railway Track（CC0 1.0）を複製・回転し、直線区間につないだものです。[配布ページ](https://grab3d.com/models/props/railway-track/)
- `crossing.glb` は Innerscene の Level crossing barrier（CC0 1.0）。遮断機を開いた状態に変更しました。[配布ページ](https://www.innerscene.com/tools/library/3d-parts/level-crossing-barrier-e3a613cb)
- `shrine.glb` は初期配置に使った Shinto Shrine by vinish.dev（CC BY 4.0、モデル未改変）。[配布ページ](https://vinish.dev/free-3d-models/japanese-buildings)
- `oak.glb` と `beech.glb` は Spreading Oak と Beech Tree by vinish.dev（CC BY 4.0、モデル未改変）。[配布ページ](https://vinish.dev/free-3d-models/broadleaf-trees)
- `shrine-grove-oak.glb` と `shrine-grove-beech.glb` は同じ樹木モデル（CC BY 4.0）の幹と葉の色を境内用に調整したものです。[配布ページ](https://vinish.dev/free-3d-models/broadleaf-trees)
- `natural-beech.glb` と `natural-plane.glb` は tevy の European Beech と Plane Tree の LOD2（CC0 1.0）です。メッシュとテクスチャを読み込み可能な形式に変換し、色を調整しました。[European Beech](https://tevy.dev/models/european-beech) ／ [Plane Tree](https://tevy.dev/models/plane-tree)
- `wild-meadow-grass.glb` と `wild-meadow-grass-far.glb` は tevy の Wild Grass Clump の LOD1 と LOD2（CC0 1.0）です。メッシュとテクスチャを変換し、色を調整しました。植生ブラシの「道端の草」が使用します。[Wild Grass Clump](https://tevy.dev/models/grass-clump)
- `torii.glb` は初期配置に使った Embossify の Torii gate（CC0 1.0）。朱色を頂点カラーからマテリアルに変更しました。[配布ページ](https://www.embossify.com/models/torii-gate)
- `wood.jpg` は Poly Haven の Wood Planks、作者は Amal Kumar（CC0 1.0）。[配布ページ](https://polyhaven.com/a/wood_planks)
- 「夏見」の駅名標、木橋、稲、東屋、民家、農具小屋、電柱と電線、石垣、野花などの小物は、この仮マップ用に作成したモデルです。
- 境内の拝殿、鳥居、手水舎、賽銭箱、石畳と石段、柵、苔石、低木、シダはこの仮マップ用に作成したモデルです。
- `valley-*` は夏見の里用に作成した竹藪、土手の石積み、礫、案内板、小物、外周の山林です。田園風景の見せ方には『NOSTALGIC TRAIN』の公式資料を参考にしました。[AMATA Games プレスキット](https://amata.games/support/press-kit/nostalgic-train/)
- `valley-weathered-stone.png` / `valley-mountain-ground.png` は新規作成した石肌と斜面のテクスチャです。遠景の森には、tevy の樹木（CC0 1.0）から描画した `valley-forest-atlas.png` を使っています。
- `asset/Audio/rural-town/river.wav` は Siddharth Patil（Ksd5）の Swale.ogg（CC0 1.0）。抜粋をモノラル WAV に変換し、音量とループのつなぎ目を調整しました。[配布ページ](https://commons.wikimedia.org/wiki/File:Swale.ogg)
- `asset/Audio/rural-town/frogs.wav` は Koba-chan の Frogs note02.ogg。抜粋をモノラル WAV に変換し、音量とループのつなぎ目を調整しました。この WAV は [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) で配布します。[配布ページ](https://commons.wikimedia.org/wiki/File:Frogs_note02.ogg)

[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) ／ [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
