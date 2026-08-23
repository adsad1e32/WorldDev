import sys

sys.argv = [__file__, "duplicate"]

import runpy

runpy.run_path(__file__.replace("ApplyOfficialBrushMaterialsDuplicate.py", "ApplyOfficialBrushMaterials.py"), run_name="__main__")
