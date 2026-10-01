"""
TMVA Hyperparameter Optimization Framework
==========================================

A comprehensive hyperparameter optimization framework for TMVA machine learning
in particle physics.

Author: acschick@jlab.org
License: MIT
"""

from setuptools import setup, find_packages
from pathlib import Path

# Read long description from README
this_directory = Path(__file__).parent
long_description = (this_directory / "README.md").read_text()

setup(
    name="tmva-hpo",
    version="1.0.0",
    author="Your Name",
    author_email="acschick@jlab.org",
    description="Hyperparameter optimization framework for TMVA in particle physics",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/yourusername/tmva-hpo",
    project_urls={
        "Bug Tracker": "https://github.com/yourusername/tmva-hpo/issues",
        "Documentation": "https://github.com/yourusername/tmva-hpo/blob/main/README.md",
        "Source Code": "https://github.com/yourusername/tmva-hpo",
    },
    
    # Package discovery
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    
    # Include non-Python files
    include_package_data=True,
    package_data={
        "": ["*.ini", "*.json", "*.md"],
    },
    
    # Entry points for command-line scripts
    scripts=["bin/tmva-hpo"],
    
    # Dependencies
    install_requires=[
        "numpy>=1.18.0",
        "pandas>=1.0.0",
        "matplotlib>=3.1.0",
        "seaborn>=0.10.0",
    ],
    
    # Optional dependencies
    extras_require={
        "dev": [
            "pytest>=6.0.0",
            "pytest-cov>=2.10.0",
            "black>=20.8b1",
            "flake8>=3.8.0",
            "sphinx>=3.2.0",
        ],
        "docs": [
            "sphinx>=3.2.0",
            "sphinx-rtd-theme>=0.5.0",
        ],
    },
    
    # Python version requirement
    python_requires=">=3.7",
    
    # PyPI classifiers
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Topic :: Scientific/Engineering :: Physics",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
    ],
    
    # Keywords for PyPI search
    keywords="tmva hpo hyperparameter-optimization machine-learning particle-physics root",
    
    # License
    license="MIT",
    
    # Project status
    platforms=["Linux", "Unix"],
)
