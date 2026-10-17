# Redux documentation

The UTF-8 text files are the editable, reviewed sources used by the in-game
readers and the PDFs. Form feeds separate chapters; chapter numbers are retained
from the original manuals. Menu help keeps the original highlight escapes.

These began with the embedded Corncob 3.42 and Other Worlds manuals. Spelling,
missing words, controls and descriptions of changed Redux rules have been
reviewed. Original legal text, credits, source tables, order forms and obsolete
contact details are retained as historical material and labelled accordingly.

Regenerate the PDFs and embedded bundle with:

```sh
python3 tools/rebuild_documentation.py
```

The tool needs only Python's standard library. PDFs have linked contents and
chapter bookmarks, and are checked in so users and builds need no PDF toolchain.
The game exports all four PDFs to `documentation` inside its save directory.
