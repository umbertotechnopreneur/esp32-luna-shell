# QSee declarative GUI source

`root.json` is the editable canonical GUI document. The firmware never stages
that monolith: `scripts/generate_qsee_gui_documents.py` produces a small
bootstrap at `package/res/root.json` and one `package/res/screen_*.json`
document per lazy content screen.

After changing the canonical document or a referenced template, regenerate and
verify the package:

```powershell
python .\scripts\generate_qsee_gui_documents.py
python .\scripts\generate_qsee_gui_documents.py --check
```

The bootstrap retains the background, Welcome, persistent chrome, and their
flows. At runtime QSee loads one additional screen document at a time.
The five Bluetooth/Wi-Fi provisioning stages and their result state deliberately
share `screen_pairing.json`: navigation inside that wizard updates one mounted
surface, while leaving the wizard unloads the shared document.
