import json
from typing import List, Dict, Any
import numpy as np
import matplotlib.pyplot as plt

def load_data(filename: str) -> List[int]:
    """Load data from JSON file."""
    with open(filename, 'r') as f:
        data = json.load(f)
    return data

def compute_stats(data: List[int]) -> Dict[str, Any]:
    """Compute statistical measures for the data."""
    data_array = np.array(data)
    
    stats = {
        'count': len(data),
        'mean': float(np.mean(data_array)),
        'median': float(np.median(data_array)),
        'std': float(np.std(data_array)),
        'min': float(np.min(data_array)),
        'max': float(np.max(data_array)),
        'p95': float(np.percentile(data_array, 95)),
        'p99': float(np.percentile(data_array, 99)),
        'p99.9': float(np.percentile(data_array, 99.9))
    }
    
    return stats

def plot_histograms(data1: List[int], data2: List[int], label1: str, label2: str) -> None:
    """Plot mirrored histograms (one positive, one negative) with log scale."""
    plt.figure(figsize=(12, 8))
    
    # Convert to numpy arrays for easier handling
    arr1 = np.array(data1)
    arr2 = np.array(data2)
    
    # Focus on 99th percentile range to see main distribution better
    p99_1 = np.percentile(arr1, 99)
    p99_2 = np.percentile(arr2, 99)
    p1_1 = np.percentile(arr1, 1)
    p1_2 = np.percentile(arr2, 1)
    
    zoom_min = min(p1_1, p1_2)
    zoom_max = max(p99_1, p99_2)
    
    # Filter data to zoom range
    arr1_zoom = arr1[(arr1 >= zoom_min) & (arr1 <= zoom_max)]
    arr2_zoom = arr2[(arr2 >= zoom_min) & (arr2 <= zoom_max)]
    
    # Use optimal binning with common bin edges
    bins = max(int(np.sqrt(len(arr1)) * 2), int(np.sqrt(len(arr2)) * 2), 100)
    bin_edges = np.linspace(zoom_min, zoom_max, bins)
    
    # Calculate histograms
    counts1, _ = np.histogram(arr1_zoom, bins=bin_edges, density=True)
    counts2, _ = np.histogram(arr2_zoom, bins=bin_edges, density=True)
    
    # Bin centers for plotting
    bin_centers = (bin_edges[:-1] + bin_edges[1:]) / 2
    bin_width = bin_edges[1] - bin_edges[0]
    
    # Plot first histogram (positive values) on top
    plt.bar(bin_centers, counts1, width=bin_width*0.8, alpha=0.7, color='blue', 
            label=f'{label1} (top)', edgecolor='none')
    
    # Plot second histogram (negative values) on bottom  
    plt.bar(bin_centers, -counts2, width=bin_width*0.8, alpha=0.7, color='red',
            label=f'{label2} (bottom)', edgecolor='none')
    
    # Set log scale for y-axis (need to handle negative values)
    plt.yscale('symlog', linthresh=1e-9)
    
    # Add zero line
    plt.axhline(y=0, color='black', linewidth=1, alpha=0.8)
    
    # Add statistical markers
    mean1 = np.mean(arr1_zoom)
    median1 = np.median(arr1_zoom)
    mean2 = np.mean(arr2_zoom)
    median2 = np.median(arr2_zoom)
    
    # Get y-limits for positioning vertical lines
    y_max = np.max(counts1)
    y_min = -np.max(counts2)
    
    plt.axvline(mean1, color='darkblue', linestyle='--', alpha=0.8, 
                ymin=0.5, ymax=1.0, label=f'{label1} Mean: {mean1:,.0f}ns')
    plt.axvline(median1, color='darkblue', linestyle='-', alpha=0.8,
                ymin=0.5, ymax=1.0, label=f'{label1} Median: {median1:,.0f}ns')
    
    plt.axvline(mean2, color='darkred', linestyle='--', alpha=0.8,
                ymin=0.0, ymax=0.5, label=f'{label2} Mean: {mean2:,.0f}ns')
    plt.axvline(median2, color='darkred', linestyle='-', alpha=0.8,
                ymin=0.0, ymax=0.5, label=f'{label2} Median: {median2:,.0f}ns')
    
    plt.xlabel('Time (nanoseconds)')
    plt.ylabel('Probability Density (log scale)')
    plt.title(f'Mirrored Latency Distributions\\n{label1} (top) vs {label2} (bottom)\\n(1st-99th percentile)')
    plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # Save to file instead of showing
    filename = 'timing_histograms.png'
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.close()  # Close the figure to free memory
    print(f"Mirrored latency histograms saved to: {filename}")
    
    # Print some additional insights
    print(f"\n{'='*60}")
    print("HISTOGRAM INSIGHTS")
    print(f"{'='*60}")
    print(f"{label1} range: {np.min(arr1):,.0f} - {np.max(arr1):,.0f} ns")
    print(f"{label2} range: {np.min(arr2):,.0f} - {np.max(arr2):,.0f} ns")
    print(f"Range overlap: {max(np.min(arr1), np.min(arr2)):,.0f} - {min(np.max(arr1), np.max(arr2)):,.0f} ns")

def print_comparison(stats1: Dict[str, Any], stats2: Dict[str, Any], label1: str, label2: str) -> None:
    """Print comparison of two datasets."""
    print(f"\n{'='*60}")
    print(f"COMPARISON: {label1} vs {label2}")
    print(f"{'='*60}")
    
    metrics = ['count', 'mean', 'median', 'std', 'min', 'max', 'p95', 'p99', 'p99.9']
    
    print(f"{'Metric':<12} {'':>20} {'':>20} {'Difference':<20} {'Ratio':<10}")
    print(f"{'':^12} {label1:>20} {label2:>20} {'(2-1)':<20} {'(2/1)':<10}")
    print("-" * 85)
    
    for metric in metrics:
        val1 = stats1[metric]
        val2 = stats2[metric]
        
        if metric == 'count':
            diff = val2 - val1
            ratio = val2 / val1 if val1 != 0 else float('inf')
            print(f"{metric:<12} {val1:>20.0f} {val2:>20.0f} {diff:>+20.0f} {ratio:>10.2f}")
        else:
            diff = val2 - val1
            ratio = val2 / val1 if val1 != 0 else float('inf')
            print(f"{metric:<12} {val1:>20.0f} {val2:>20.0f} {diff:>+20.0f} {ratio:>10.2f}")

def main() -> None:
    """Main analysis function."""
    print("Loading data...")
    
    # Load the datasets
    no_exceptions = load_data('results_no_exceptions.json')
    with_exceptions = load_data('results_with_exceptions.json')
    
    print(f"Loaded {len(no_exceptions)} samples from results_no_exceptions.json")
    print(f"Loaded {len(with_exceptions)} samples from results_with_exceptions.json")
    
    # Compute statistics
    print("\nComputing statistics...")
    stats_no_exceptions = compute_stats(no_exceptions)
    stats_with_exceptions = compute_stats(with_exceptions)
    
    # Print individual statistics
    print(f"\n{'='*60}")
    print("STATISTICS FOR RESULTS_NO_EXCEPTIONS.JSON")
    print(f"{'='*60}")
    for key, value in stats_no_exceptions.items():
        if key == 'count':
            print(f"{key:<12}: {value:>20.0f}")
        else:
            print(f"{key:<12}: {value:>20.0f}")
    
    print(f"\n{'='*60}")
    print("STATISTICS FOR RESULTS_WITH_EXCEPTIONS.JSON")
    print(f"{'='*60}")
    for key, value in stats_with_exceptions.items():
        if key == 'count':
            print(f"{key:<12}: {value:>20.0f}")
        else:
            print(f"{key:<12}: {value:>20.0f}")
    
    # Print comparison
    print_comparison(stats_no_exceptions, stats_with_exceptions, 
                    "No Exceptions", "With Exceptions")
    
    # Plot histograms
    print("\nGenerating histograms...")
    plot_histograms(no_exceptions, with_exceptions, 
                   "No Exceptions", "With Exceptions")

if __name__ == "__main__":
    main()
