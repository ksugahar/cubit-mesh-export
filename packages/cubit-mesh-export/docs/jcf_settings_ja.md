# JMAG JCF 出力用の設定 JSON

このページは、AI エージェントが Cubit モデルから JMAG の静磁場解析用 `.jcf` を生成するための入力仕様です。出力前に実モデルのブロック・sideset と、ユーザーが指定した解析条件を照合します。

## エージェントの作業手順

1. Cubit のモデルからブロック名と sideset 名を取得する。メッシュ要素がブロックに割り当てられ、使用する sideset にメッシュ面があることを確認する。
2. `settings.json` をひな形にし、実モデルに合わせて `unit`、材料、励磁、境界を記述する。すべてのブロックを `block_materials` に含める。材料特性、電流、巻数、励磁方向、対称面の根拠が不足していれば、値を確定する前に確認する。
3. JSON として読み込み、ブロック名・sideset 名の完全一致と、参照先の材料名を確認する。UTF-8 で保存し、コメントや末尾カンマを入れない。
4. Cubit の **Export → JMAG (.jcf)** で設定ファイルを選び、新しい出力先を指定する。Cubit 内の Python から実行する場合は、読み込んだ設定辞書を `cubit_mesh_export.jmag.export_jcf(path, settings)` に渡す。別ファイルの材料辞書を使う場合、Python 呼び出し側で先に読み込む。
5. 出力が生成されたことを確認する。出力内容の検証が必要なら、JCF に含まれる材料・励磁・境界の設定を照合する。静解析の確認には JMAG ExecSolver の実行結果を使う。

## 基本例

[settings.json をダウンロード](https://raw.githubusercontent.com/ksugahar/cubit-mesh-export/refs/heads/codex/standalone-baseline/packages/cubit-mesh-export/docs/settings.json) · [GitHub で内容を表示](settings.json)

このファイルは下の基本例と同一です。Cubit に `air` と `coil` のブロック、`symmetry_plane` の sideset があり、例の解析条件を使う場合はそのまま選択できます。別のモデルでは名前、座標、物理条件を合わせてください。

```json
{
  "unit": "mm",
  "material_library": {
    "air_material": {"mu_r": 1},
    "coil_material": {"mu_r": 1, "resistivity": 1.7e-8}
  },
  "block_materials": {
    "air": "air_material",
    "coil": "coil_material"
  },
  "coils": [
    {
      "blocks": ["coil"],
      "current": 1.5,
      "turns": 10,
      "direction": [0, 1, 0],
      "origin": [5, 0, 0]
    }
  ],
  "boundaries": {"symmetry_plane": "symmetry"}
}
```

| キー | 指定する内容 |
| --- | --- |
| `unit` | Cubit 座標とコイルの `origin` の長さ単位。`m`、`cm`、`mm`、`um` が指定でき、省略時は `mm`。座標値に合わせて明示する。 |
| `material_library` | 材料名から材料特性への対応。上のように直接書くか、メニューから出力する場合は設定 JSON を起点とする相対パスで別の JSON を指定できる。 |
| `block_materials` | Cubit のブロック名から材料名への対応。全ブロックに指定する。 |
| `coils` | コイルごとの設定。コイルを使わなければ省略できる。 |
| `boundaries` | Cubit の sideset 名から境界の種類への対応。2.3.0 は `"symmetry"` のみ。外表面のメッシュ面を含む sideset を指定する。対称境界を指定しない場合は省略できる。 |

ブロック名と sideset 名は、大文字・小文字も含めて Cubit と一致させます。同名の対象が複数ある場合は出力エラーになります。対称境界の指定には `boundaries` を使います。旧キーの `symmetry_sidesets` は入力エラーになります。

## 材料

- 線形材料は `mu_r` に比透磁率を指定します。コイルに割り当てる材料には `resistivity` を Ω·m で指定します。
- 非線形磁性材は `bh` に `[H, B]` の行を並べます。単位は A/m と T です。各列は厳密に増加させ、2行以上を用意します。積層率を指定する場合は `lamination_factor` に百分率を入れます。
- 永久磁石は `magnet` に `br`（T）、`hcb`（A/m、正の大きさ）、`direction`（着磁方向の3成分）を指定します。同じ材料に `bh` と `magnet` を併記すると入力エラーになります。

メニューで別ファイルを使う場合は、上の設定例の `material_library` の値を `"materials.json"` に置き換え、同じフォルダーの `materials.json` に次のように材料辞書を保存します。

```json
{
  "air_material": {"mu_r": 1},
  "coil_material": {"mu_r": 1, "resistivity": 1.7e-8}
}
```

## コイルの励磁

`blocks` はコイルを構成するブロック名の配列、`current` は A 単位の電流、`turns` は巻数です。巻数を省略すると1巻になります。励磁の位置と方向は、モデルに応じて次のどちらかの方法で記述します。

- 座標で指定する場合は、`direction` と `origin` を指定します。どちらも3成分の配列です。
- メッシュ面で指定する場合は、`flowin` に sideset 名と電流が面を横切る方向 `normal` を指定します。その方向に外向き法線を持つコイル要素の面が選ばれます。例えば、次は `coil_cut` を +Y 方向に横切る電流です。

```json
{
  "blocks": ["coil"],
  "current": 1.5,
  "turns": 10,
  "flowin": {"sideset": "coil_cut", "normal": [0, 1, 0]}
}
```

## 一様外部磁場

外部磁場を加える場合は、`settings.json` の最上位に次のメンバーを追加します。`value` の単位は T です。

```json
{
  "external_field": {"axis": [0, 0, 1], "value": 0.05}
}
```
