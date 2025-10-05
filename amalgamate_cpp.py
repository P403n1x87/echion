#!/usr/bin/env python3
"""
Script to generate a single C++ amalgamated file from echion source files.

This script:
1. Finds all .h, .cc, .cpp files in echion/ directory
2. Parses #include dependencies between header files
3. Performs topological sort to determine correct inclusion order
4. Removes 'inline' keyword from functions, globals, methods (keeps static fields inline)
5. Removes 'static' keyword from functions, globals (keeps static fields/methods static)
6. Generates a single .cpp file with all content
"""

import os
import re
import sys
from collections import defaultdict, deque
from pathlib import Path
from typing import Dict, List, Set, Tuple


class DependencyAnalyzer:
    def __init__(self, source_dir: str):
        self.source_dir = Path(source_dir)
        self.header_files = []
        self.cpp_files = []
        self.dependencies = defaultdict(set)  # file -> set of files it depends on
        self.file_contents = {}
        
    def find_source_files(self):
        """Find all .h, .cc, .cpp files in echion/ directory."""
        for file_path in self.source_dir.rglob('*.h'):
            self.header_files.append(file_path)
            
        for file_path in self.source_dir.rglob('*.cc'):
            self.cpp_files.append(file_path)
            
        for file_path in self.source_dir.rglob('*.cpp'):
            self.cpp_files.append(file_path)
            
        print(f"Found {len(self.header_files)} header files")
        print(f"Found {len(self.cpp_files)} C++ source files")
        
    def read_file_contents(self):
        """Read contents of all source files."""
        all_files = self.header_files + self.cpp_files
        for file_path in all_files:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    self.file_contents[file_path] = f.read()
            except Exception as e:
                print(f"Warning: Could not read {file_path}: {e}")
                
    def parse_includes(self):
        """Parse #include dependencies from all files."""
        include_pattern = re.compile(r'^\s*#include\s*<echion/([^>]+)>\s*$', re.MULTILINE)
        
        for file_path, content in self.file_contents.items():
            matches = include_pattern.findall(content)
            for include_file in matches:
                # Find the actual file path for this include
                include_path = self.source_dir / include_file
                if include_path.exists():
                    self.dependencies[file_path].add(include_path)
                else:
                    print(f"Warning: Include file {include_path} not found for {file_path}")
                    
    def topological_sort(self) -> List[Path]:
        """Perform topological sort on header files based on dependencies."""
        # Build in-degree count for each header file
        in_degree = defaultdict(int)
        graph = defaultdict(set)
        
        # Only consider header files for topological sorting
        header_set = set(self.header_files)
        
        for file_path in self.header_files:
            in_degree[file_path] = 0
            
        for file_path, deps in self.dependencies.items():
            if file_path in header_set:
                for dep in deps:
                    if dep in header_set:
                        graph[dep].add(file_path)
                        in_degree[file_path] += 1
                        
        # Kahn's algorithm
        queue = deque([f for f in self.header_files if in_degree[f] == 0])
        result = []
        
        while queue:
            current = queue.popleft()
            result.append(current)
            
            for neighbor in graph[current]:
                in_degree[neighbor] -= 1
                if in_degree[neighbor] == 0:
                    queue.append(neighbor)
                    
        if len(result) != len(self.header_files):
            print("Warning: Circular dependency detected in header files!")
            # Add remaining files to avoid missing them
            for f in self.header_files:
                if f not in result:
                    result.append(f)
                    
        return result
        
    def transform_content(self, content: str, file_path: Path) -> str:
        """Transform content by removing inline/static keywords as specified."""
        lines = content.split('\n')
        transformed_lines = []
        
        for line in lines:
            original_line = line
            
            # Only skip #include <echion/...> lines - we're expanding these
            # But preserve all conditional compilation and other includes
            if re.match(r'^\s*#include\s*<echion/', line):
                transformed_lines.append(f"// {line}  // Expanded inline")
                continue
                
            # Skip #pragma once - not needed in amalgamated file
            if re.match(r'^\s*#pragma\s+once\s*$', line):
                transformed_lines.append(f"// {line}  // Removed for amalgamation")
                continue
            
            # Transform keywords based on context
            is_class_member = self._is_class_member_context(lines, transformed_lines)
            is_inside_function = self._is_inside_function_context(lines, transformed_lines)
            
            # Check for static inline class members first - preserve them completely
            # Be more aggressive about preserving static inline - if we see the pattern, assume it's a class member
            if re.search(r'\bstatic\s+inline\b', line):
                # Keep static inline patterns unchanged (assume they're class members)
                pass
            else:
                # Transform inline keywords 
                if 'inline' in line and not line.strip().startswith('//'):
                    # Remove inline from global functions and variables
                    line = re.sub(r'\binline\s+', '', line)
                    
                # Transform static keywords - be very conservative
                if 'static' in line and not line.strip().startswith('//'):
                    stripped = line.strip()
                    
                    # Only remove static from very specific global patterns
                    # Be extremely conservative to avoid removing static from class methods
                    if (
                        # Global static functions - starting at beginning of line, no indentation
                        re.match(r'^static\s+\w+.*\(.*\)\s*{?\s*$', stripped) or
                        # Global static variables - starting at beginning of line, no indentation  
                        re.match(r'^static\s+\w+.*=', stripped) or
                        re.match(r'^static\s+\w+\s+\w+\s*;', stripped)
                    ):
                        # This is very likely a global static that should be removed
                        line = re.sub(r'\bstatic\s+', '', line)
                    # Otherwise, keep the static keyword (safer approach)
                    
            transformed_lines.append(line)
            
        return '\n'.join(transformed_lines)
        
    def _is_class_member_context(self, all_lines: List[str], processed_lines: List[str]) -> bool:
        """Check if we're currently inside a class definition."""
        # Look at more recent lines for class/struct definition
        recent_lines = processed_lines[-200:] if len(processed_lines) >= 200 else processed_lines
        
        brace_depth = 0
        in_class_or_struct = False
        
        for line in recent_lines:
            stripped = line.strip()
            
            # Skip empty lines and comments
            if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
                continue
                
            # Count braces
            open_braces = stripped.count('{')
            close_braces = stripped.count('}')
            brace_depth += open_braces - close_braces
            
            # Check for class/struct definition
            if re.search(r'\b(class|struct)\s+\w+', stripped):
                in_class_or_struct = True
                
            # If we hit a closing brace that brings us to level 0 or below, we're out of class
            if brace_depth <= 0 and in_class_or_struct:
                in_class_or_struct = False
                brace_depth = 0  # Reset to prevent negative depths
                
        return in_class_or_struct and brace_depth > 0
        
    def _is_inside_function_context(self, all_lines: List[str], processed_lines: List[str]) -> bool:
        """Check if we're currently inside a function body."""
        # Look at recent lines for function definition
        recent_lines = processed_lines[-30:] if len(processed_lines) >= 30 else processed_lines
        
        brace_level = 0
        in_function = False
        
        for line in recent_lines:
            stripped = line.strip()
            
            # Count braces to track nesting level
            brace_level += stripped.count('{') - stripped.count('}')
            
            # Check for function definition (has parentheses and opening brace)
            # But not class/struct definitions
            if (re.search(r'\w+\s*\([^)]*\)\s*{', stripped) and 
                not re.search(r'\b(class|struct|enum)\b', stripped)):
                in_function = True
                
            # Also check for function definitions split across lines
            elif (re.search(r'\w+\s*\([^)]*\)\s*$', stripped) and 
                  not re.search(r'\b(class|struct|enum|if|while|for|switch)\b', stripped)):
                in_function = True
                
            # If we've closed all braces, we're no longer in function
            if brace_level <= 0 and in_function:
                in_function = False
                
        return in_function and brace_level > 0
        
    def generate_amalgamated_file(self, output_path: str):
        """Generate the final amalgamated C++ file."""
        print(f"Generating amalgamated file: {output_path}")
        
        with open(output_path, 'w', encoding='utf-8') as out_file:
            # Write header
            out_file.write("""// Amalgamated C++ file generated from echion sources
// This file contains all header and source files combined into a single unit
// Generated by amalgamate_cpp.py

""")
            
            # Write header files in dependency order (with all their conditional includes)
            sorted_headers = self.topological_sort()
            
            out_file.write("// ============================================================================\n")
            out_file.write("// HEADER FILES (in dependency order)\n") 
            out_file.write("// ============================================================================\n\n")
            
            for header_file in sorted_headers:
                if header_file in self.file_contents:
                    out_file.write(f"// ----------------------------------------------------------------------------\n")
                    out_file.write(f"// Content from: {header_file.relative_to(self.source_dir.parent)}\n")
                    out_file.write(f"// ----------------------------------------------------------------------------\n\n")
                    
                    transformed_content = self.transform_content(
                        self.file_contents[header_file], header_file
                    )
                    out_file.write(transformed_content)
                    out_file.write("\n\n")
                    
            # Write source files
            out_file.write("// ============================================================================\n")
            out_file.write("// SOURCE FILES\n")
            out_file.write("// ============================================================================\n\n")
            
            for cpp_file in self.cpp_files:
                if cpp_file in self.file_contents:
                    out_file.write(f"// ----------------------------------------------------------------------------\n")
                    out_file.write(f"// Content from: {cpp_file.relative_to(self.source_dir.parent)}\n") 
                    out_file.write(f"// ----------------------------------------------------------------------------\n\n")
                    
                    transformed_content = self.transform_content(
                        self.file_contents[cpp_file], cpp_file
                    )
                    out_file.write(transformed_content)
                    out_file.write("\n\n")
                    
        print(f"Amalgamated file generated successfully: {output_path}")


def main():
    if len(sys.argv) > 1:
        echion_dir = sys.argv[1]
    else:
        echion_dir = "echion"
        
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    else:
        output_file = "echion_amalgamated.cpp"
        
    if not os.path.exists(echion_dir):
        print(f"Error: Directory {echion_dir} does not exist")
        return 1
        
    analyzer = DependencyAnalyzer(echion_dir)
    
    print("Analyzing echion source files...")
    analyzer.find_source_files()
    analyzer.read_file_contents()
    analyzer.parse_includes()
    
    print("Performing topological sort on dependencies...")
    sorted_headers = analyzer.topological_sort()
    print(f"Header dependency order: {[h.name for h in sorted_headers]}")
    
    analyzer.generate_amalgamated_file(output_file)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())