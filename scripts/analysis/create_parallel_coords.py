#!/usr/bin/env python3
"""
Create parallel coordinate plots for TMVA HPO results.

This script generates parallel coordinate plots showing relationships
between hyperparameters, features, and performance metrics.
Outputs to PDF for easy viewing over SSH.
"""

import json
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
matplotlib.use('Agg')  # Non-interactive backend for PDF generation
from pandas.plotting import parallel_coordinates
import numpy as np
from pathlib import Path
import argparse


def create_parallel_coords(data_file: str, output_pdf: str, model_type: str, 
                          color_by: str = "roc_auc", top_n: int = None):
    """Create parallel coordinate plot from exported data.
    
    Args:
        data_file: Path to JSON or CSV data file
        output_pdf: Path to save PDF output
        model_type: "MLP" or "BDT"
        color_by: Metric to use for color coding (roc_auc or separation)
        top_n: Only plot top N jobs by performance (None = all jobs)
    """
    print(f"Creating parallel coordinate plot for {model_type}...")
    
    # Load data
    data_path = Path(data_file)
    if data_path.suffix == '.json':
        with open(data_path, 'r') as f:
            data = json.load(f)
        df = pd.DataFrame(data)
    elif data_path.suffix == '.csv':
        df = pd.read_csv(data_path)
    else:
        raise ValueError(f"Unsupported file format: {data_path.suffix}")
    
    print(f"Loaded {len(df)} jobs")
    
    # Filter to top N if requested
    if top_n and top_n < len(df):
        df = df.nlargest(top_n, color_by)
        print(f"Filtered to top {top_n} jobs by {color_by}")
    
    # Prepare data for plotting
    plot_df = df.copy()
    
    # Select columns for parallel coordinates
    columns = []
    
    # Training sample size (if present)
    if 'train_events' in df.columns:
        # Convert to numeric if string
        if df['train_events'].dtype == 'object':
            plot_df['train_events'] = df['train_events'].astype(str).str.replace(',', '').astype(int)
        columns.append('train_events')
    
    # Number of features
    if 'num_features' in df.columns:
        columns.append('num_features')
    
    # MLP hyperparameters
    if 'mlp_ncycles' in df.columns:
        columns.append('mlp_ncycles')
    
    if 'mlp_testrate' in df.columns:
        columns.append('mlp_testrate')
    
    # MLP neuron type (categorical - convert to numeric)
    if 'mlp_neuron' in df.columns:
        neuron_types = sorted(df['mlp_neuron'].unique())
        neuron_map = {nt: i for i, nt in enumerate(neuron_types)}
        plot_df['neuron_type'] = df['mlp_neuron'].map(neuron_map)
        columns.append('neuron_type')
    
    # BDT hyperparameters
    if 'bdt_ntrees' in df.columns:
        columns.append('bdt_ntrees')
    
    if 'bdt_maxdepth' in df.columns:
        columns.append('bdt_maxdepth')
    
    # Performance metrics
    if 'roc_auc' in df.columns:
        columns.append('roc_auc')
    
    if 'separation' in df.columns:
        columns.append('separation')
    
    # Add class column for coloring
    plot_df['performance'] = df[color_by]
    columns.append('performance')
    
    # Normalize all columns to [0, 1] for better visualization
    plot_df_norm = plot_df[columns].copy()
    for col in columns:
        col_min = plot_df_norm[col].min()
        col_max = plot_df_norm[col].max()
        if col_max > col_min:
            plot_df_norm[col] = (plot_df_norm[col] - col_min) / (col_max - col_min)
        else:
            plot_df_norm[col] = 0.5  # All values the same
    
    # Create figure
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Create parallel coordinates plot
    parallel_coordinates(
        plot_df_norm, 
        'performance', 
        cols=[c for c in columns if c != 'performance'],
        ax=ax,
        colormap='viridis',
        alpha=0.3,
        linewidth=0.5
    )
    
    # Customize plot
    title_suffix = f" (Top {top_n})" if top_n else ""
    ax.set_title(f"TMVA {model_type} Hyperparameter Analysis{title_suffix}", 
                 fontsize=14, fontweight='bold')
    ax.set_ylabel('Normalized Value', fontsize=12)
    ax.grid(True, alpha=0.3)
    ax.legend().remove()  # Remove automatic legend
    
    # Add colorbar
    sm = plt.cm.ScalarMappable(
        cmap='viridis',
        norm=plt.Normalize(vmin=df[color_by].min(), vmax=df[color_by].max())
    )
    sm.set_array([])
    cbar = plt.colorbar(sm, ax=ax)
    cbar.set_label(color_by.replace('_', ' ').title(), fontsize=12)
    
    plt.tight_layout()
    
    # Save to PDF
    output_path = Path(output_pdf)
    plt.savefig(str(output_path), format='pdf', dpi=300, bbox_inches='tight')
    plt.close()
    
    print(f"✓ Saved plot to: {output_path}")
    
    # Print summary statistics
    print(f"\nSummary Statistics:")
    print(f"  {color_by}: min={df[color_by].min():.6f}, max={df[color_by].max():.6f}, mean={df[color_by].mean():.6f}")
    
    return output_path


def create_feature_importance_plot(data_file: str, output_pdf: str, model_type: str):
    """Create plot showing impact of individual features on performance.
    
    Args:
        data_file: Path to JSON or CSV data file
        output_pdf: Path to save PDF output
        model_type: "MLP" or "BDT"
    """
    print(f"\nCreating feature importance analysis for {model_type}...")
    
    # Load data
    data_path = Path(data_file)
    if data_path.suffix == '.json':
        with open(data_path, 'r') as f:
            data = json.load(f)
        df = pd.DataFrame(data)
    else:
        df = pd.read_csv(data_path)
    
    # Feature columns
    feature_cols = [col for col in df.columns if col.startswith('has_')]
    
    if not feature_cols:
        print("  ✗ No feature columns found")
        return
    
    # Calculate mean ROC AUC when each feature is present vs absent
    feature_impact = []
    
    for feature in feature_cols:
        present = df[df[feature] == 1]['roc_auc'].mean()
        absent = df[df[feature] == 0]['roc_auc'].mean()
        impact = present - absent
        
        feature_impact.append({
            'feature': feature.replace('has_', ''),
            'present_mean': present,
            'absent_mean': absent,
            'impact': impact,
            'n_present': (df[feature] == 1).sum(),
            'n_absent': (df[feature] == 0).sum()
        })
    
    impact_df = pd.DataFrame(feature_impact)
    impact_df = impact_df.sort_values('impact', ascending=False)
    
    # Create bar plot
    fig, ax = plt.subplots(figsize=(12, 6))
    
    colors = ['green' if x > 0 else 'red' for x in impact_df['impact']]
    bars = ax.bar(range(len(impact_df)), impact_df['impact'], color=colors, alpha=0.7)
    
    # Customize plot
    ax.set_xticks(range(len(impact_df)))
    ax.set_xticklabels(impact_df['feature'], rotation=45, ha='right')
    ax.set_ylabel('Impact on ROC AUC', fontsize=12)
    ax.set_title(f"Feature Impact on {model_type} ROC AUC (Present vs Absent)", 
                 fontsize=14, fontweight='bold')
    ax.axhline(y=0, color='black', linestyle='-', linewidth=0.5)
    ax.grid(True, alpha=0.3, axis='y')
    
    # Add value labels on bars
    for i, (bar, val) in enumerate(zip(bars, impact_df['impact'])):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{val:+.6f}',
                ha='center', va='bottom' if height > 0 else 'top',
                fontsize=8)
    
    plt.tight_layout()
    
    # Save to PDF
    output_path = Path(output_pdf)
    plt.savefig(str(output_path), format='pdf', dpi=300, bbox_inches='tight')
    plt.close()
    
    print(f"✓ Saved feature importance plot to: {output_path}")
    
    # Print results
    print("\nFeature Impact Analysis:")
    for _, row in impact_df.iterrows():
        print(f"  {row['feature']:20s}: {row['impact']:+.6f} "
              f"(present: {row['present_mean']:.6f}, absent: {row['absent_mean']:.6f})")


def main():
    parser = argparse.ArgumentParser(description='Create parallel coordinate plots for TMVA HPO')
    parser.add_argument('--data-dir', default='jobs', help='Directory containing exported data')
    parser.add_argument('--output-dir', default='plots', help='Directory to save plots')
    parser.add_argument('--top-n', type=int, help='Only plot top N jobs by ROC AUC')
    parser.add_argument('--color-by', default='roc_auc', 
                       choices=['roc_auc', 'separation'],
                       help='Metric to use for color coding')
    
    args = parser.parse_args()
    
    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True)
    
    # Process BDT data
    bdt_csv = Path(args.data_dir) / "parallel_coords_data_bdt.csv"
    if bdt_csv.exists():
        create_parallel_coords(
            str(bdt_csv),
            str(output_dir / "parallel_coords_bdt.pdf"),
            "BDT",
            color_by=args.color_by,
            top_n=args.top_n
        )
        
        create_feature_importance_plot(
            str(bdt_csv),
            str(output_dir / "feature_importance_bdt.pdf"),
            "BDT"
        )
    else:
        print(f"✗ BDT data not found: {bdt_csv}")
    
    # Process MLP data
    mlp_csv = Path(args.data_dir) / "parallel_coords_data_mlp.csv"
    if mlp_csv.exists():
        create_parallel_coords(
            str(mlp_csv),
            str(output_dir / "parallel_coords_mlp.pdf"),
            "MLP",
            color_by=args.color_by,
            top_n=args.top_n
        )
        
        create_feature_importance_plot(
            str(mlp_csv),
            str(output_dir / "feature_importance_mlp.pdf"),
            "MLP"
        )
    else:
        print(f"✗ MLP data not found: {mlp_csv}")
    
    print("\n✓ All plots created successfully!")
    print(f"  PDF files saved in {output_dir}")
    print(f"  Use 'rsync -avz {output_dir}/*.pdf your_local_machine:destination/' to transfer")


if __name__ == '__main__':
    main()
