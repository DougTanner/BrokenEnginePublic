import re
import os
from pathlib import Path
from dataclasses import dataclass
from typing import List, Dict, Set
from collections import defaultdict
import argparse

@dataclass
class Usage:
    file_path: str
    line_number: int
    original_line: str
    old_name: str
    new_name: str

class StringViewHungarianFixer:
    def __init__(self, root_dir: str, dry_run: bool = True):
        self.root_dir = Path(root_dir)
        self.dry_run = dry_run
        self.excludes = ['ThirdParty']

        # Patterns for string_view declarations with pc prefix
        self.declaration_patterns = [
            r'std::string_view\s+\b(pc[A-Z][a-zA-Z0-9]*)\b',
            r'std::wstring_view\s+\b(pc[A-Z][a-zA-Z0-9]*)\b',
            r'std::u32string_view\s+\b(pc[A-Z][a-zA-Z0-9]*)\b',
        ]

        # Patterns for constexpr declarations with kpc prefix
        self.constexpr_patterns = [
            r'constexpr\s+std::string_view\s+\b(kpc[A-Z][a-zA-Z0-9]*)\b',
            r'inline\s+constexpr\s+std::string_view\s+\b(kpc[A-Z][a-zA-Z0-9]*)\b',
            r'static\s+inline\s+constexpr\s+std::string_view\s+\b(kpc[A-Z][a-zA-Z0-9]*)\b',
        ]

        self.variables_to_rename: Dict[str, str] = {}
        self.all_usages: List[Usage] = []

    def should_exclude(self, file_path: Path) -> bool:
        for exclude in self.excludes:
            if exclude in file_path.parts:
                return True
        return False

    def get_cpp_files(self) -> List[Path]:
        files = []
        for pattern in ['**/*.h', '**/*.cpp']:
            for file_path in self.root_dir.glob(pattern):
                if not self.should_exclude(file_path):
                    files.append(file_path)
        return files

    def convert_name(self, old_name: str) -> str:
        if old_name.startswith('kpc'):
            rest = old_name[3:]
            return 'k' + rest
        elif old_name.startswith('pc'):
            rest = old_name[2:]
            return rest[0].lower() + rest[1:] if len(rest) > 1 else rest.lower()
        return old_name

    def find_declarations(self):
        files = self.get_cpp_files()

        for file_path in files:
            try:
                content = file_path.read_text(encoding='utf-8', errors='replace')

                for pattern in self.constexpr_patterns + self.declaration_patterns:
                    for match in re.finditer(pattern, content):
                        old_name = match.group(1)
                        if old_name not in self.variables_to_rename:
                            self.variables_to_rename[old_name] = self.convert_name(old_name)
            except Exception as e:
                print(f"Error reading {file_path}: {e}")

    def find_all_usages(self):
        files = self.get_cpp_files()
        names = list(self.variables_to_rename.keys())
        if not names:
            return

        pattern = r'\b(' + '|'.join(re.escape(name) for name in names) + r')\b'

        for file_path in files:
            try:
                content = file_path.read_text(encoding='utf-8', errors='replace')
                lines = content.split('\n')

                for line_num, line in enumerate(lines, 1):
                    for match in re.finditer(pattern, line):
                        old_name = match.group(1)
                        self.all_usages.append(Usage(
                            file_path=str(file_path),
                            line_number=line_num,
                            original_line=line,
                            old_name=old_name,
                            new_name=self.variables_to_rename[old_name]
                        ))
            except Exception as e:
                print(f"Error reading {file_path}: {e}")

    def generate_preview(self) -> str:
        report = ["=" * 80, "STRING_VIEW HUNGARIAN NOTATION FIX - PREVIEW", "=" * 80, ""]

        report.append("VARIABLES TO RENAME:")
        for old, new in sorted(self.variables_to_rename.items()):
            report.append(f"  {old} -> {new}")
        report.append("")

        usages_by_file: Dict[str, List[Usage]] = defaultdict(list)
        for usage in self.all_usages:
            usages_by_file[usage.file_path].append(usage)

        report.append("CHANGES BY FILE:")
        for file_path, usages in sorted(usages_by_file.items()):
            report.append(f"\n{file_path} ({len(usages)} changes):")
            for usage in sorted(usages, key=lambda u: u.line_number):
                new_line = re.sub(r'\b' + re.escape(usage.old_name) + r'\b', usage.new_name, usage.original_line)
                report.append(f"  L{usage.line_number}: {usage.original_line.strip()}")
                report.append(f"       -> {new_line.strip()}")

        report.extend(["", "=" * 80, f"Variables: {len(self.variables_to_rename)} | Usages: {len(self.all_usages)} | Files: {len(usages_by_file)}", "=" * 80])
        return "\n".join(report)

    def apply_changes(self):
        if self.dry_run:
            print("DRY RUN - No changes made. Use --apply to make changes.")
            return

        usages_by_file: Dict[str, List[Usage]] = defaultdict(list)
        for usage in self.all_usages:
            usages_by_file[usage.file_path].append(usage)

        for file_path, usages in usages_by_file.items():
            try:
                path = Path(file_path)
                content = path.read_text(encoding='utf-8', errors='replace')

                for old_name, new_name in self.variables_to_rename.items():
                    content = re.sub(r'\b' + re.escape(old_name) + r'\b', new_name, content)

                path.write_text(content, encoding='utf-8')
                print(f"Updated: {file_path}")
            except Exception as e:
                print(f"Error updating {file_path}: {e}")

    def run(self):
        print("Phase 1: Finding declarations...")
        self.find_declarations()
        print(f"Found {len(self.variables_to_rename)} variables to rename")

        print("Phase 2: Finding all usages...")
        self.find_all_usages()
        print(f"Found {len(self.all_usages)} usages")

        print("Phase 3: Generating preview...")
        preview = self.generate_preview()
        print(preview)

        preview_path = self.root_dir / "hungarian_fix_preview.txt"
        preview_path.write_text(preview, encoding='utf-8')
        print(f"\nPreview saved to: {preview_path}")

        if not self.dry_run:
            print("\nPhase 4: Applying changes...")
            self.apply_changes()
            print("Done!")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Fix string_view Hungarian notation')
    parser.add_argument('--root', default='.', help='Root directory')
    parser.add_argument('--apply', action='store_true', help='Apply changes (default: dry-run)')
    args = parser.parse_args()

    fixer = StringViewHungarianFixer(args.root, dry_run=not args.apply)
    fixer.run()
