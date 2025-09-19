import sys
sys.dont_write_bytecode = True

from pathlib import Path
import shutil
import subprocess
import pre_qc
import os

def _clear_directory(dir_path: Path) -> None:
	for item in dir_path.iterdir():
		if item.is_dir():
			shutil.rmtree(item)
		else:
			item.unlink(missing_ok=True)

def main() -> None:
	script_dir = Path(__file__).resolve().parent

	object_dir = script_dir / "object"
	if object_dir.exists():
		_clear_directory(object_dir)
	else:
		object_dir.mkdir(parents=True, exist_ok=True)
		
	pre_qc_dir = script_dir / "pre_qc"
	if pre_qc_dir.exists():
		_clear_directory(pre_qc_dir)
	else:
		pre_qc_dir.mkdir(parents=True, exist_ok=True)

	qc_exe = script_dir / ("qc.exe" if sys.platform.startswith("win") else "qc")
	
	with open("locale_list") as file:
		for line in file:
			print(f"Compiling {line}...")
			r = pre_qc.run(line)
			if r:
				filename = os.path.join("pre_qc", line)
				
			subprocess.run([str(qc_exe), str(filename.strip())], check=True)


if __name__ == "__main__":
	main()
