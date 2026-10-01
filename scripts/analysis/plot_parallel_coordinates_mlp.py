#!/usr/bin/env python3
"""
Create parallel coordinate plot for MLP HPO results
"""

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
matplotlib.use('pdf')  # Use PDF backend for vector graphics
import numpy as np
from matplotlib.path import Path
import matplotlib.patches as patches

# Read the corrected results
df = pd.read_csv('mlp_hpo_results_corrected.csv')

print(f"Total jobs: {len(df)}")
print(f"ROC AUC range: {df['roc_auc'].min():.6f} - {df['roc_auc'].max():.6f}")

# Check VarTransform distribution
print(f"\nVarTransform distribution:")
print(df['mlp_vartransform'].value_counts())

# Take top 100 for clarity
df_plot = df.head(100).copy()

# Check VarTransform distribution
print(f"\nVarTransform distribution:")
print(df['mlp_vartransform'].value_counts())

# Take top 100 for clarity
df_plot = df.head(100).copy()

# Convert categorical variables to numeric for plotting
df_plot['hidden_num'] = df_plot['mlp_hidden'].apply(lambda x: int(x.replace('N+', '')) if 'N+' in x else 0)
df_plot['neuron_map'] = df_plot['mlp_neuron'].map({'tanh': 0, 'sigmoid': 1, 'ReLU': 2, 'linear': 3})
df_plot['n_features'] = df_plot['feature_set'].apply(lambda x: x.count('1') if isinstance(x, str) else 0)
df_plot['train_k'] = df_plot['train_events'].astype(int) / 1000  # In thousands

# Define axes - omit VarTransform since 99.95% are 'G'
axes_config = [
    ('n_features', 'N Features', df_plot['n_features'].min(), df_plot['n_features'].max()),
    ('hidden_num', 'Hidden\n(N+X)', df_plot['hidden_num'].min(), df_plot['hidden_num'].max()),
    ('neuron_map', 'Neuron', 0, 3),  # We'll use custom labels
    ('mlp_ncycles', 'NCycles', df_plot['mlp_ncycles'].min(), df_plot['mlp_ncycles'].max()),
    ('train_k', 'Train Events\n(thousands)', df_plot['train_k'].min(), df_plot['train_k'].max()),
    ('roc_auc', 'ROC AUC', df_plot['roc_auc'].min(), df_plot['roc_auc'].max())
]

n_axes = len(axes_config)

# Create figure
fig, host_ax = plt.subplots(figsize=(14, 8))

# Generate colormap based on ROC AUC (performance)
cmap = plt.cm.RdYlGn
norm = matplotlib.colors.Normalize(vmin=df_plot['roc_auc'].min(), vmax=df_plot['roc_auc'].max())

# Draw each configuration as a line
for idx, row in df_plot.iterrows():
    # Create path through all axes
    ys = []
    for col, label, vmin, vmax in axes_config:
        value = row[col]
        # Normalize to [0, 1]
        normalized = (value - vmin) / (vmax - vmin) if vmax > vmin else 0.5
        ys.append(normalized)
    
    # Color by ROC AUC
    color = cmap(norm(row['roc_auc']))
    
    # Plot line
    xs = np.arange(n_axes)
    host_ax.plot(xs, ys, c=color, alpha=0.4, linewidth=1.5)

# Set up the axes
host_ax.set_xlim(-0.5, n_axes - 0.5)
host_ax.set_ylim(0, 1)
host_ax.set_xticks(range(n_axes))
host_ax.set_xticklabels([label for col, label, vmin, vmax in axes_config], fontsize=11)

# Remove y-axis from host (we'll draw custom ones)
host_ax.yaxis.set_visible(False)
host_ax.spines['right'].set_visible(False)
host_ax.spines['left'].set_visible(False)

# Draw vertical axes for each dimension
for i, (col, label, vmin, vmax) in enumerate(axes_config):
    # Draw vertical line
    host_ax.axvline(i, color='black', linewidth=1.5, zorder=1)
    
    # Get unique values for this dimension
    unique_vals = sorted(df_plot[col].unique())
    
    # Draw tick marks and labels
    if col == 'neuron_map':
        # Special handling for neuron type
        tick_positions = [0, 1, 2, 3]
        tick_labels = ['tanh', 'sigmoid', 'ReLU', 'linear']
        for val, tick_label in zip(tick_positions, tick_labels):
            y_pos = (val - vmin) / (vmax - vmin) if vmax > vmin else 0.5
            host_ax.text(i + 0.05, y_pos, tick_label, fontsize=8, va='center')
            host_ax.plot([i - 0.02, i + 0.02], [y_pos, y_pos], 'k-', linewidth=1)
    elif len(unique_vals) <= 8:
        # For discrete values, show all
        for val in unique_vals:
            y_pos = (val - vmin) / (vmax - vmin) if vmax > vmin else 0.5
            display_val = int(val) if col in ['n_features', 'hidden_num', 'mlp_ncycles'] else f'{val:.0f}'
            host_ax.text(i + 0.05, y_pos, display_val, fontsize=8, va='center')
            host_ax.plot([i - 0.02, i + 0.02], [y_pos, y_pos], 'k-', linewidth=1)
    else:
        # For continuous, show min, quartiles, max
        percentiles = [0, 25, 50, 75, 100]
        for p in percentiles:
            val = np.percentile(df_plot[col], p)
            y_pos = (val - vmin) / (vmax - vmin) if vmax > vmin else 0.5
            if col == 'roc_auc':
                display_val = f'{val:.4f}'
            elif col == 'train_k':
                display_val = f'{val:.0f}'
            else:
                display_val = f'{val:.1f}'
            host_ax.text(i + 0.05, y_pos, display_val, fontsize=8, va='center')
            host_ax.plot([i - 0.02, i + 0.02], [y_pos, y_pos], 'k-', linewidth=1)

# Add colorbar
sm = plt.cm.ScalarMappable(cmap=cmap, norm=norm)
sm.set_array([])
cbar = plt.colorbar(sm, ax=host_ax, pad=0.02)
cbar.set_label('ROC AUC', fontsize=12, fontweight='bold')

# Title
host_ax.set_title('MLP Hyperparameter Optimization - Top 100 Configurations', 
                   fontsize=14, fontweight='bold', pad=20)

plt.tight_layout()
plt.savefig('mlp_parallel_coordinates_top100.pdf', bbox_inches='tight')
print("\n✓ Saved: mlp_parallel_coordinates_top100.pdf")
plt.close()

# Also create version with all data
fig2, host_ax2 = plt.subplots(figsize=(14, 8))

# Process all data
df['hidden_num'] = df['mlp_hidden'].apply(lambda x: int(x.replace('N+', '')) if 'N+' in x else 0)
df['neuron_map'] = df['mlp_neuron'].map({'tanh': 0, 'sigmoid': 1, 'ReLU': 2, 'linear': 3})
df['n_features'] = df['feature_set'].apply(lambda x: x.count('1') if isinstance(x, str) else 0)
df['train_k'] = df['train_events'].astype(int) / 1000

# Redefine axes with full data range
axes_config_all = [
    ('n_features', 'N Features', df['n_features'].min(), df['n_features'].max()),
    ('hidden_num', 'Hidden\n(N+X)', df['hidden_num'].min(), df['hidden_num'].max()),
    ('neuron_map', 'Neuron', 0, 3),
    ('mlp_ncycles', 'NCycles', df['mlp_ncycles'].min(), df['mlp_ncycles'].max()),
    ('train_k', 'Train Events\n(thousands)', df['train_k'].min(), df['train_k'].max()),
    ('roc_auc', 'ROC AUC', df['roc_auc'].min(), df['roc_auc'].max())
]

norm_all = matplotlib.colors.Normalize(vmin=df['roc_auc'].min(), vmax=df['roc_auc'].max())

# Draw lines (subsample for visibility)
sample_df = df.sample(min(500, len(df)), random_state=42)
for idx, row in sample_df.iterrows():
    ys = []
    for col, label, vmin, vmax in axes_config_all:
        value = row[col]
        normalized = (value - vmin) / (vmax - vmin) if vmax > vmin else 0.5
        ys.append(normalized)
    
    color = cmap(norm_all(row['roc_auc']))
    xs = np.arange(n_axes)
    host_ax2.plot(xs, ys, c=color, alpha=0.2, linewidth=0.8)

# Set up axes
host_ax2.set_xlim(-0.5, n_axes - 0.5)
host_ax2.set_ylim(0, 1)
host_ax2.set_xticks(range(n_axes))
host_ax2.set_xticklabels([label for col, label, vmin, vmax in axes_config_all], fontsize=11)
host_ax2.yaxis.set_visible(False)
host_ax2.spines['right'].set_visible(False)
host_ax2.spines['left'].set_visible(False)

# Draw vertical axes
for i, (col, label, vmin, vmax) in enumerate(axes_config_all):
    host_ax2.axvline(i, color='black', linewidth=1.5, zorder=1)
    
    if col == 'neuron_map':
        tick_positions = [0, 1, 2, 3]
        tick_labels = ['tanh', 'sigmoid', 'ReLU', 'linear']
        for val, tick_label in zip(tick_positions, tick_labels):
            y_pos = (val - vmin) / (vmax - vmin) if vmax > vmin else 0.5
            host_ax2.text(i + 0.05, y_pos, tick_label, fontsize=8, va='center')
            host_ax2.plot([i - 0.02, i + 0.02], [y_pos, y_pos], 'k-', linewidth=1)
    else:
        percentiles = [0, 25, 50, 75, 100]
        for p in percentiles:
            val = np.percentile(df[col], p)
            y_pos = (val - vmin) / (vmax - vmin) if vmax > vmin else 0.5
            if col == 'roc_auc':
                display_val = f'{val:.4f}'
            elif col in ['n_features', 'hidden_num', 'mlp_ncycles']:
                display_val = f'{int(val)}'
            elif col == 'train_k':
                display_val = f'{val:.0f}'
            else:
                display_val = f'{val:.1f}'
            host_ax2.text(i + 0.05, y_pos, display_val, fontsize=8, va='center')
            host_ax2.plot([i - 0.02, i + 0.02], [y_pos, y_pos], 'k-', linewidth=1)

# Add colorbar
sm2 = plt.cm.ScalarMappable(cmap=cmap, norm=norm_all)
sm2.set_array([])
cbar2 = plt.colorbar(sm2, ax=host_ax2, pad=0.02)
cbar2.set_label('ROC AUC', fontsize=12, fontweight='bold')

host_ax2.set_title(f'MLP Hyperparameter Optimization - 500 Random Configurations (of {len(df)} total)', 
                    fontsize=14, fontweight='bold', pad=20)

plt.tight_layout()
plt.savefig('mlp_parallel_coordinates_all.pdf', bbox_inches='tight')
print("✓ Saved: mlp_parallel_coordinates_all.pdf")

plt.tight_layout()
plt.savefig('mlp_parallel_coordinates_all.pdf', bbox_inches='tight')
print("✓ Saved: mlp_parallel_coordinates_all.pdf")

# Print summary statistics
print("\n=== Summary Statistics ===")
print(f"\nTop 10 configurations:")
print(df[['job_id', 'roc_auc', 'mlp_hidden', 'mlp_ncycles', 'mlp_neuron', 'n_features', 'train_events']].head(10).to_string(index=False))

print("\n✓ Analysis complete")

