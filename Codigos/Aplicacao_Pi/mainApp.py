import sys
from PySide6 import QtCore, QtWidgets, QtGui
from GUI.mainAlbatrozGUI import AlbatrozGUI

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    window = AlbatrozGUI()
    window.show()
    sys.exit(app.exec())    