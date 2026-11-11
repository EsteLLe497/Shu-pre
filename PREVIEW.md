# GM31 Asset Preview

同じ DirectX 11 レンダラーで、モーション・表情・エフェクトを共通の時刻にそろえて確認する制作ツールです。

## 起動

Visual Studio のデバッグ設定で、コマンド引数に `--preview`、作業ディレクトリに `$(ProjectDir)` を指定して起動します。引数を外すと通常のゲームを起動します。

プロジェクトフォルダーから起動する場合は次のコマンドを使います。

```powershell
.\x64\Debug\GM31_Game.exe --preview
```

Timeline には Play、Pause、Stop、前後1コマ、ループ、速度、各レイヤーのチェックボックスと Solo があります。色の付いた行をクリックまたはドラッグすると、その時刻を表示します。エフェクトの開始時刻と位置、カメラは Inspector で調整できます。

プレビュー画面の余白では、**Alt + 左ドラッグ**で回転、**中ドラッグ**で平行移動、**ホイール / Alt + 右ドラッグ**でズームできます。**F** で初期の構図に戻ります。

Inspector の **Toon shader** は本編と同じ設定を使います。起動時に `toonSettings.cfg` を読み込み、**Load game preset** で読み直せます。**Save to game** は外観だけを共有ファイルへ保存し、本編で保存済みのカメラを保持します。

## Blender からモデルを送る

1. Blender のプリファレンスから `tools/blender/export_to_preview.py` をインストールし、有効にします。
2. キャラのメッシュとアーマチュアを同じコレクションにまとめ、書き出すオブジェクトを表示します。
3. 3D ビューの N パネルにある **Game Preview** で、`GM31_Game.vcxproj` のあるフォルダーとキャラのコレクションを指定します。
4. **Export to Preview** を押すと、`asset/model/Player_Movement/Player_Preview.fbx` に書き出されます。
5. プレビューの **Reload Blender export** を押します。表情欄のモーフ数と **Shape key** 一覧に必要なキーがあることを確認します。

モデル読み込みやテクスチャ、既存モーションの照合に失敗した場合は、プレビュー画面に理由を表示し、直前のモデルを使い続けます。

## まばたきの例

Blender の顔メッシュでまばたき用シェイプキーの **Value** を動かして形を確認します。Blender のタイムラインでも試すなら、60 fps で1フレーム目を0、5フレーム目を1、12フレーム目を0にし、各フレームの Value の上で `I` を押します。

ゲームのプレビューでは同じシェイプキーを **Shape key** で選び、**Start = 0.000 秒、Peak = 0.067 秒、End = 0.183 秒、Weight = 1.0** を設定します。**Base expression** は普段の表情です。**Smooth blend** は開始・最大・終了付近で加減速し、元の表情から指定の表情へ滑らかに移行します。Blender の書き出しは表情の形を渡し、プレビューの3つの時刻が重みを決めます。Blender で付けたキーフレーム曲線の自動取り込みは、この版では未対応です。

Inspector の **Expression → Add Shape Key** で、主キーに最大16個のキーを重ねられます。例えば口の笑顔に目や眉のキーを足し、各行の **Key weight** で強さを調整します。追加したキーはタイムラインに別の行として表示されます。各キーの **Start / Peak / End / Key weight / Smooth blend** を個別に調整できます。タイムラインの金色のひし形はピークを示し、クリックするとその秒数へ移動します。**Remove** で行を削除できます。追加キーも **Export Action** に含まれます。

## 表情とエフェクトを本編へ渡す

1. プレビューでモーション・表情・エフェクトを調整します。
2. Timeline の **Action name** に名前を入力し、**Export Action** を押します。`asset/actions/<名前>.json` に保存されます。J で試す場合は `Attack01` と入力します。
3. 本編を起動し直し、**J** で発射します。発射を起点に、保存したクリップ・表情・エフェクトを再生します。発射そのものは従来どおり即時です。
4. 調整を再開するには **Load Action** を押し、保存済み一覧から名前を選びます。

表情が有効なアクションでは、本編も同じ `Player_Preview.fbx` を使います。対象のモデルやシェイプキーが見つからない場合は通常モデルを使い、モーションとエフェクトは利用できます。モーションをオフにすると、本編では普段の移動モーションを続けます。

名前ごとにファイルを保存します。同じ名前で書き出すとそのファイルを更新します。Solo・Loop・Speed はプレビュー用で、本編は1倍速で1回再生します。再生中に J を続けて押しても、弾は発射され、開始済みの演出は最後まで進みます。

## Mixamo からモーションを追加する

Mixamo では既存のキャラと互換性のあるリグを使います。**Download Settings** は **FBX Binary (.fbx)**、**Without Skin**、**30 FPS**、**Keyframe Reduction は None** から試します。短い走りをプレビューで比較するときは、モーション設定に **In Place** があればオンにします。

1. ファイルを `asset/model/Player_Movement/Motions/Female_Run_01.fbx` のように置きます。ファイル名には英数字、空白、ハイフン、アンダースコアが使えます。
2. Asset Preview の **Inspector → Motion → Refresh motions** を押し、**Clip** で選びます。
3. アクションに使うなら、名前を付けて **Export Action** で保存します。JSON にはファイルの拡張子を除いた名前が入ります。ゲームでも同じ FBX が必要です。

気に入ったクリップを通常の走りに使う場合は、`Motions/Run.fbx` として置いて本編を起動し直します。`Walk.fbx` と `Idle.fbx` でも同様です。読めない差し替えは標準クリップ（Idle.fbx、FemaleWalk.fbx、FemaleRun.fbx）に戻ります。本編に対応するアクションJSONがある場合は、そこで選んだクリップを優先します。走りの足滑りを調整したいときは、移動量のあるダウンロード版も試します。対応するルートの移動量があれば、本編はそこから移動速度を求めます。

## C++ からアクションを呼び出す

`ActionFile::load(path, sequence)` は指定した JSON を検証して読み込みます。プレイヤー用には、JSON と FBX・表情の参照をまとめて検証する `Player::loadAction()` が使えます。読み込みは初期化時に行い、イベントでは `Player::playAction()` を呼びます。

```cpp
// シーン初期化時。以後も使うデータとして保持する。
ActionSequence celebration;
const bool ready = player->loadAction("asset/actions/Celebration.json", celebration);

// イベント発生時。
if (ready)
    player->playAction(celebration);
```

固定の J キー用ファイルは `Attack01.json` です。別の名前で書き出したアクションは、JSON のパスを指定して呼び出します。名前には英数字、空白、ハイフン、アンダースコアが使えます。

## 本編プレイヤーの状態とアクション

| 状態 | アクション |
| --- | --- |
| 通常待機 | `asset/actions/Idle.json` |
| 歩く | `asset/actions/Walk.json` |
| Shift を押しながら走る | `asset/actions/Run.json` |
| 地上で10秒操作なし | `asset/actions/akubi.json` |
| J で発射 | `asset/actions/Attack01.json` |

待機・歩き・走りは対応するアクションをループします。あくびは同じ無操作中に一度だけ再生し、移動・ジャンプ・発射・カメラ操作などが入れば中断します。アクションを呼ぶタイミングは `Player.cpp` の `Update()` にあり、JSON との対応は `PlayerActions.cpp` の `definitions` にあります。登録済みの単発アクションは `playAction(PlayerActionKind::yawn)`、任意のアクションは `loadAction(path, action)` で先読みしてから `playAction(action)` で再生できます。表情やクリップを調整した場合は上の名前で書き出し、本編を起動し直します。 本編の待機・歩き・走りはFBXの周期で体を再生し、表情とエフェクトにはJSONの周期を使います。入力による移動速度は Player.h で指定します。本編の歩き・走りでは、ループの境目でモデルが後ろへ跳ねないように、FBXが1周で進む水平移動だけを描画から差し引きます。足や体の回転、上下動はFBXのキーを使います。

## コードの役割

- `PreviewTimeline` が再生時刻、各レイヤーの有効状態と表情の重みを計算します。
- `PreviewPanel` が ImGui で編集と表示を担当します。
- `PreviewScene` がモデル・カメラ・エフェクトを所有し、時刻に対応した姿勢を描画します。
- `AnimationModel` と `Explosion` はゲーム本編と共通です。表情はスキニング前に合成し、エフェクトは現在時刻からスプライト番号を求めます。

この版はモーション1本、表情キーごとに独立した区間、エフェクト1イベントを同時に試す構成です。`ActionSequence` が表情の補間と本編の再生時刻を扱い、`ActionFile` が JSON の入出力と検証を担当します。
