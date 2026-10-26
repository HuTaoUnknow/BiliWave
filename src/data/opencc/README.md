# Embedded Traditional-to-Simplified dictionaries

These unmodified UTF-8 data files come from OpenCC, maintained by BYVoid and contributors:

- Release: `ver.1.1.9`
- Source: https://github.com/BYVoid/OpenCC/tree/ver.1.1.9/data/dictionary
- License: Apache-2.0; full text in `docs/licenses/OpenCC-Apache-2.0.txt`

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| TSCharacters.txt | 34627 | 6b5a0a799bea2bb22c001f635eaa3fc2904310f0c08addbff275477a80ecf09a |
| TSPhrases.txt | 5181 | 504169029c43f7f234b8e2ae470720af3657675c5574ff8aa0feb257e1dc5ce2 |

BiliWave embeds both files as Qt resources in the executable. Its own UTF-16 trie matches the longest dictionary entry and uses the first listed conversion, with phrase entries taking precedence. For example, `後` becomes `后`, `乾淨` becomes `干净`, and `乾隆` / `乾坤` keep their dictionary-defined spelling. The files are data only; the OpenCC library and its transitive dependencies are not bundled.
