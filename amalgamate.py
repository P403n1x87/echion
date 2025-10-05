#!/usr/bin/env python3
"""
Amalgamation script for .cc files.
Creates a single source file from multiple .cc files that can be compiled in one invocation.
"""

import os
import re
from pathlib import Path
from typing import List, Set, Tuple, Dict
from collections import defaultdict, deque


def find_source_files(directory: Path) -> Tuple[List[Path], List[Path]]:
    """Find all .h and .cc files in the directory (including subdirs)."""
    h_files = sorted(directory.glob("**/*.h"))
    cc_files = sorted(directory.glob("**/*.cc"))
    return h_files, cc_files


def get_echion_dependencies(file_path: Path, h_file_paths: Dict[str, Path]) -> List[str]:
    """Extract echion/ header dependencies from a file."""
    dependencies: List[str] = []
    
    with open(file_path, 'r', encoding='utf-8') as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith('#include <echion/'):
                # Extract path like "cache.h" or "cpython/tasks.h" from "#include <echion/...>"
                match = re.search(r'<echion/([^>]+)>', stripped)
                if match:
                    include_path = match.group(1)
                    if include_path in h_file_paths:
                        dependencies.append(include_path)
    
    return dependencies


def topological_sort_headers(h_files: List[Path], source_dir: Path) -> List[Path]:
    """Sort header files in dependency order using topological sort."""
    # Build a map from relative path (from echion/) to Path
    # e.g., "cache.h" or "cpython/tasks.h" -> Path object
    path_to_file: Dict[str, Path] = {}
    for f in h_files:
        rel_path = f.relative_to(source_dir)
        path_to_file[str(rel_path)] = f
    
    # Build dependency graph: for each file, track what it depends on
    dependencies: Dict[str, List[str]] = {}
    # Also build reverse graph: for each file, track what depends on it
    dependents: Dict[str, List[str]] = defaultdict(list)
    in_degree: Dict[str, int] = {}
    
    for h_file in h_files:
        rel_path = str(h_file.relative_to(source_dir))
        deps = get_echion_dependencies(h_file, path_to_file)
        dependencies[rel_path] = deps
        in_degree[rel_path] = len(deps)
        
        for dep in deps:
            dependents[dep].append(rel_path)
    
    # Perform topological sort (Kahn's algorithm)
    # Start with nodes that have no dependencies (in_degree == 0)
    queue: deque[str] = deque()
    
    for path in dependencies:
        if in_degree[path] == 0:
            queue.append(path)
    
    sorted_paths: List[str] = []
    
    while queue:
        # Sort queue for deterministic output
        current = queue.popleft()
        sorted_paths.append(current)
        
        # For each node that depends on current, reduce its in-degree
        for dependent in dependents.get(current, []):
            in_degree[dependent] -= 1
            if in_degree[dependent] == 0:
                queue.append(dependent)
    
    # Check for cycles
    if len(sorted_paths) != len(h_files):
        print("Warning: Circular dependencies detected, using alphabetical order for remaining files")
        for h_file in h_files:
            rel_path = str(h_file.relative_to(source_dir))
            if rel_path not in sorted_paths:
                sorted_paths.append(rel_path)
    
    # Convert back to Path objects
    sorted_files = [path_to_file[path] for path in sorted_paths]
    
    return sorted_files


def extract_includes_and_code(file_path: Path) -> Tuple[List[str], List[str], str]:
    """
    Extract system includes, local includes, and code from a file.
    Keeps preprocessor conditionals with the code.
    
    Returns:
        Tuple of (system_includes, local_includes, code_with_conditional_includes)
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    lines = content.split('\n')
    system_includes: List[str] = []
    local_includes: List[str] = []
    code_lines: List[str] = []
    
    # Track conditional depth
    conditional_depth = 0
    
    for line in lines:
        stripped = line.strip()
        
        # Track conditional depth
        if stripped.startswith('#if'):
            conditional_depth += 1
            code_lines.append(line)
        elif stripped.startswith('#endif'):
            conditional_depth -= 1
            code_lines.append(line)
        elif stripped.startswith('#elif') or stripped.startswith('#else'):
            code_lines.append(line)
        elif stripped == '#pragma once':
            # Skip pragma once directives in amalgamation
            continue
        elif conditional_depth > 0:
            # Inside a conditional block - keep everything including includes
            # But skip echion/ includes
            if stripped.startswith('#include <echion/'):
                local_includes.append(line)
                # Replace with a comment so we don't lose the conditional structure
                continue
            else:
                code_lines.append(line)
        elif stripped.startswith('#include <echion/'):
            # Skip echion/ includes - headers are embedded in amalgamation
            local_includes.append(line)
            continue
        elif stripped.startswith('#include <'):
            # Top-level system include
            system_includes.append(line)
        elif stripped.startswith('#include "'):
            # Top-level local include
            local_includes.append(line)
        else:
            # Regular code or other preprocessor directives
            code_lines.append(line)
    
    # Remove leading static or inline keywords from code lines
    processed_lines: List[str] = []
    
    for line in code_lines:
        stripped = line.strip()
        leading_whitespace = line[:len(line) - len(line.lstrip())]
        
        # Check if this is likely a method definition (contains ::)
        is_method = '::' in stripped
        
        # Always remove inline from function/method definitions
        modified_line = re.sub(r'^(\s*)inline\s+', r'\1', line)
        
        # Only remove static if:
        # - Line doesn't contain :: (not a method)
        # - AND line has minimal indentation (likely file scope, not inside class)
        if not is_method and len(leading_whitespace) == 0:
            # At file scope (no indentation), remove static
            modified_line = re.sub(r'^(\s*)static\s+', r'\1', modified_line)
        
        processed_lines.append(modified_line)
    
    code = '\n'.join(processed_lines)
    return system_includes, local_includes, code


def amalgamate(source_dir: Path, output_file: Path) -> None:
    """
    Create an amalgamation of all .h and .cc files in source_dir.
    
    Args:
        source_dir: Directory containing .h and .cc files
        output_file: Output file path for the amalgamation
    """
    h_files, cc_files = find_source_files(source_dir)
    
    if not h_files and not cc_files:
        print(f"No .h or .cc files found in {source_dir}")
        return
    
    print(f"Found {len(h_files)} .h files and {len(cc_files)} .cc files")
    
    # Sort headers in dependency order
    if h_files:
        print("Sorting headers by dependencies...")
        h_files = topological_sort_headers(h_files, source_dir)
        print("  Header files (in dependency order):")
        for f in h_files:
            rel_path = f.relative_to(source_dir)
            print(f"    - {rel_path}")
    
    if cc_files:
        print("  Source files:")
        for f in cc_files:
            print(f"    - {f.name}")
    
    # Collect all includes and code sections
    all_system_includes: Set[str] = set()
    all_local_includes: Set[str] = set()
    header_sections: List[Tuple[Path, str]] = []
    code_sections: List[Tuple[Path, str]] = []
    
    # Process header files first
    for h_file in h_files:
        rel_path = h_file.relative_to(source_dir)
        print(f"Processing {rel_path}...")
        system_incs, local_incs, code = extract_includes_and_code(h_file)
        all_system_includes.update(system_incs)
        all_local_includes.update(local_incs)
        header_sections.append((h_file, code))
    
    # Then process source files
    for cc_file in cc_files:
        print(f"Processing {cc_file.name}...")
        system_incs, local_incs, code = extract_includes_and_code(cc_file)
        all_system_includes.update(system_incs)
        all_local_includes.update(local_incs)
        code_sections.append((cc_file, code))
    
    # Write amalgamated file
    with open(output_file, 'w', encoding='utf-8') as f:
        # Header comment
        f.write("// Amalgamated source file generated from:\n")
        if h_files:
            f.write("// Header files:\n")
            for h_file in h_files:
                rel_path = h_file.relative_to(source_dir)
                f.write(f"//   - {rel_path}\n")
        if cc_files:
            f.write("// Source files:\n")
            for cc_file in cc_files:
                f.write(f"//   - {cc_file.name}\n")
        f.write("//\n")
        f.write("// This file is automatically generated. Do not edit directly.\n")
        f.write("\n")
        
        # Write all system includes first (sorted for consistency)
        if all_system_includes:
            f.write("// System includes\n")
            for inc in sorted(all_system_includes):
                f.write(f"{inc}\n")
            f.write("\n")
        
        # Write all local includes (excluding echion/* since we're including the headers directly)
        local_includes_filtered = {inc for inc in all_local_includes if not '<echion/' in inc}
        if local_includes_filtered:
            f.write("// Local includes\n")
            for inc in sorted(local_includes_filtered):
                f.write(f"{inc}\n")
            f.write("\n")
        
        # Write header file contents
        if header_sections:
            f.write("// " + "=" * 76 + "\n")
            f.write("// HEADER FILES\n")
            f.write("// " + "=" * 76 + "\n\n")
            for h_file, code in header_sections:
                rel_path = h_file.relative_to(source_dir)
                f.write(f"// {'=' * 76}\n")
                f.write(f"// Header: {rel_path}\n")
                f.write(f"// {'=' * 76}\n")
                f.write(code)
                f.write("\n\n")
        
        # Write source file contents
        if code_sections:
            f.write("// " + "=" * 76 + "\n")
            f.write("// SOURCE FILES\n")
            f.write("// " + "=" * 76 + "\n\n")
            for cc_file, code in code_sections:
                f.write(f"// {'=' * 76}\n")
                f.write(f"// Source: {cc_file.name}\n")
                f.write(f"// {'=' * 76}\n")
                f.write(code)
                f.write("\n\n")
    
    print(f"\nAmalgamation complete: {output_file}")
    print(f"Total size: {output_file.stat().st_size} bytes")


def main() -> None:
    """Main entry point."""
    # Default to echion directory
    script_dir = Path(__file__).parent
    source_dir = script_dir / "echion"
    output_file = script_dir / "echion_amalgamated.cc"
    
    print("Echion Amalgamation Tool")
    print("=" * 80)
    print(f"Source directory: {source_dir}")
    print(f"Output file: {output_file}")
    print()
    
    amalgamate(source_dir, output_file)


if __name__ == "__main__":
    main()

