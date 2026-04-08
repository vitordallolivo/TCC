import sys
import subprocess
import importlib.metadata
from packaging.requirements import Requirement
from pathlib import Path
import tomllib

def check_dependencies():
    """Check if all dependencies from pyproject.toml are installed"""
    
    # Find pyproject.toml (look in current directory and parent directories)
    pyproject_path = find_pyproject_toml()
    if not pyproject_path:
        print("Warning: pyproject.toml not found. Skipping dependency check.")
        return True
    
    try:
        with open(pyproject_path, "rb") as f:
            pyproject = tomllib.load(f)
    except Exception as e:
        print(f"Warning: Could not read pyproject.toml: {e}")
        return True
    
    # Extract dependencies
    dependencies = []
    try:
        if 'project' in pyproject and 'dependencies' in pyproject['project']:
            dependencies = pyproject['project']['dependencies']
    except KeyError:
        print("No dependencies found in pyproject.toml")
        return True
    
    missing = []
    version_mismatch = []
    
    print("Checking dependencies...")
    print("-" * 40)
    
    for dep_spec in dependencies:
        try:
            req = Requirement(dep_spec)
            
            # Check if package is installed
            try:
                installed_version = importlib.metadata.version(req.name)
                
                # Check version compatibility
                if req.specifier.contains(installed_version):
                    print(f"✓ {req.name} {installed_version}")
                else:
                    print(f"⚠ {req.name} {installed_version} (requires {req.specifier})")
                    version_mismatch.append((req.name, installed_version, req.specifier))
                    
            except importlib.metadata.PackageNotFoundError:
                print(f"✗ {dep_spec}")
                missing.append(dep_spec)
                
        except Exception as e:
            print(f"? Could not parse {dep_spec}: {e}")
            missing.append(dep_spec)
    
    print("-" * 40)
    return missing, version_mismatch

def find_pyproject_toml():
    """Find pyproject.toml in current directory or parents"""
    current_dir = Path(__file__).resolve().parent
    
    # Check current directory
    pyproject = current_dir / "pyproject.toml"
    if pyproject.exists():
        return pyproject
    
    # Check parent directory (in case mainApp.py is in a subfolder)
    pyproject = current_dir.parent / "pyproject.toml"
    if pyproject.exists():
        return pyproject
    
    return None

def handle_dependencies():
    """Handle dependency checking and user prompts"""
    
    # Skip check if running as frozen executable
    if getattr(sys, 'frozen', False):
        return True
    
    missing, version_mismatch = check_dependencies()
    
    if not missing and not version_mismatch:
        print("✓ All dependencies satisfied!")
        return True
    
    print("\n" + "=" * 50)
    print("DEPENDENCY ISSUES DETECTED")
    print("=" * 50)
    
    if missing:
        print(f"\nMissing packages: {', '.join(missing)}")
    
    if version_mismatch:
        print("\nVersion mismatches:")
        for name, installed, required in version_mismatch:
            print(f"  • {name}: installed {installed}, requires {required}")

    print("Trying to Install dependecies....")
    
    return install_dependencies(missing, version_mismatch)


def install_dependencies(missing, version_mismatch):
    """Install missing packages and fix version mismatches"""

    
    try:
        # Install missing packages
        if missing:
            print("\nInstalling missing packages...")
            subprocess.check_call([sys.executable, "-m", "pip", "install"] + missing)
        
        # Fix version mismatches
        for name, installed, required in version_mismatch:
            
            print(f"\nAdjusting {name} version...")

            subprocess.check_call([sys.executable, "-m", "pip", "uninstall", "-y", name]) # Uninstall current version
            subprocess.check_call([sys.executable, "-m", "pip", "install", f"{name}{required}"])  # Install correct version

        print("\n✓ All dependencies installed successfully!")
        
        # Verify installation
        print("\nVerifying installation...")
        missing, version_mismatch = check_dependencies()
        
        if not missing and not version_mismatch:
            return True
        else:
            print("Some issues remain. Please check manually.")
            return False
            
    except subprocess.CalledProcessError as e:
        print(f"\nError installing dependencies: {e}")
        return False
    except Exception as e:
        print(f"\nUnexpected error: {e}")
        return False

def main():
    """Main entry point with dependency checking"""
    
    if not handle_dependencies(): ## Checks if shit is right
        print("\nCannot start application due to dependency issues.")
        print("Please fix the issues and try again.")
        sys.exit(1)
    
    # Import here after dependencies are confirmed
    try:
        from PySide6.QtWidgets import QApplication
        from GUI.mainAlbatrozGUI import AlbatrozGUI
        
        print("\nStarting AlbatrozGUI...")
        app = QApplication(sys.argv)
        app.setStyle("Fusion")
        window = AlbatrozGUI()
        window.show()
        sys.exit(app.exec())
        
    except ImportError as e: # shit happens
        print(f"\nError importing modules: {e}")
        print("Please check your installation.")
        sys.exit(1)
    except Exception as e:
        print(f"\nUnexpected error starting application: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()