import serial
import serial.tools.list_ports
from typing import List, Optional, Dict, Any

class SerialCommunication:
    """
    A class for handling serial port communication with device control capabilities.
    """
    
    def __init__(self):
        self.serial_connection: Optional[serial.Serial] = None
        self.is_connected: bool = False
        self.current_port: Optional[str] = None
        self.current_baudrate: int = 9600  # Default baud rate
        self.connection_timeout: float = 2.0
        
        # Common baud rates for selection
        self.common_baudrates: List[int] = [
            300, 600, 1200, 2400, 4800, 9600, 14400, 19200,
            28800, 38400, 57600, 115200, 230400, 460800, 921600
        ]
    
    def list_available_ports(self) -> str:
        """
        List all available serial ports and return as a formatted string.
        
        Returns:
            str: Formatted string containing available ports with descriptions
        """
        ports = serial.tools.list_ports.comports()
        
        if not ports:
            return "No serial ports found."
        
        result = "Available serial ports:\n"
        result += "-" * 50 + "\n"
        
        for idx, port in enumerate(ports, 1):
            result += f"{idx}. Port: {port.device}\n"
            result += f"   Description: {port.description}\n"
            result += f"   HWID: {port.hwid}\n"
            result += f"   Manufacturer: {port.manufacturer}\n"
            result += f"   Product: {port.product}\n"
            result += f"   Serial Number: {port.serial_number}\n"
            result += "-" * 50 + "\n"
        
        return result
    
    def list_ports_simple(self) -> str:
        """
        Return a simple list of available port names.
        
        Returns:
            str: Simple comma-separated list of port devices
        """
        ports = serial.tools.list_ports.comports()
        port_names = [port.device for port in ports]
        
        if not port_names:
            return "No ports available"
        
        return f"Available ports: {', '.join(port_names)}"
    
    def connect(self, port: str, baudrate: int = 9600, timeout: float = 2.0) -> bool:
        """
        Connect to a serial port.
        
        Args:
            port (str): The serial port to connect to (e.g., 'COM3' or '/dev/ttyUSB0')
            baudrate (int): Baud rate for communication
            timeout (float): Read timeout in seconds
            
        Returns:
            bool: True if connection successful, False otherwise
        """
        try:
            # Close existing connection if any
            if self.serial_connection and self.serial_connection.is_open:
                self.serial_connection.close()
            
            # Create new connection
            self.serial_connection = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            
            # Test the connection
            if self.serial_connection.is_open:
                self.is_connected = True
                self.current_port = port
                self.current_baudrate = baudrate
                self.connection_timeout = timeout
                return True
            else:
                self.is_connected = False
                return False
                
        except Exception as e:
            print(f"Connection error: {e}")
            self.is_connected = False
            return False
    
    def disconnect(self) -> bool:
        """
        Disconnect from the serial port.
        
        Returns:
            bool: True if disconnection successful, False otherwise
        """
        try:
            if self.serial_connection and self.serial_connection.is_open:
                self.serial_connection.close()
                self.is_connected = False
                self.current_port = None
                return True
            return False
        except Exception as e:
            print(f"Disconnection error: {e}")
            return False
    
    def set_baudrate(self, baudrate: int) -> bool:
        """
        Set baud rate for the current connection.
        
        Args:
            baudrate (int): New baud rate to set
            
        Returns:
            bool: True if baud rate changed successfully, False otherwise
        """
        if not self.is_connected or not self.serial_connection:
            print("Not connected to any port")
            return False
        
        try:
            self.serial_connection.baudrate = baudrate
            self.current_baudrate = baudrate
            return True
        except Exception as e:
            print(f"Error setting baudrate: {e}")
            return False
    
    def get_baudrates(self) -> str:
        """
        Get a formatted string of common baud rates.
        
        Returns:
            str: Formatted string of available baud rates
        """
        result = "Common baud rates:\n"
        for baud in self.common_baudrates:
            result += f"- {baud} bps\n"
        return result
    
    def send_data(self, data: str, encoding: str = 'utf-8') -> bool:
        """
        Send data through the serial connection.
        
        Args:
            data (str): Data to send
            encoding (str): Encoding to use
            
        Returns:
            bool: True if data sent successfully, False otherwise
        """
        if not self.is_connected or not self.serial_connection:
            print("Not connected to any port")
            return False
        
        try:
            encoded_data = data.encode(encoding)
            self.serial_connection.write(encoded_data)
            return True
        except Exception as e:
            print(f"Error sending data: {e}")
            return False
    
    def read_data(self, size: int = 100, encoding: str = 'utf-8', timeout: float = None) -> Optional[str]:
        """
        Read data from the serial connection.
        
        Args:
            size (int): Number of bytes to read
            encoding (str): Encoding to use for decoding
            timeout (float): Optional custom timeout
            
        Returns:
            Optional[str]: Decoded string or None if error
        """
        if not self.is_connected or not self.serial_connection:
            print("Not connected to any port")
            return None
        
        try:
            if timeout is not None:
                original_timeout = self.serial_connection.timeout
                self.serial_connection.timeout = timeout
            
            data = self.serial_connection.read(size)
            
            if timeout is not None:
                self.serial_connection.timeout = original_timeout
            
            if data:
                return data.decode(encoding, errors='ignore')
            return ""
        except Exception as e:
            print(f"Error reading data: {e}")
            return None
    
    def send_and_receive(self, command: str, wait_time: float = 1.0, 
                         encoding: str = 'utf-8') -> Optional[str]:
        """
        Send a command and read the response.
        
        Args:
            command (str): Command to send
            wait_time (float): Time to wait for response in seconds
            encoding (str): Encoding to use
            
        Returns:
            Optional[str]: Response or None if error
        """
        if not self.send_data(command, encoding):
            return None
        
        # Small delay to allow device to respond
        import time
        time.sleep(wait_time)
        
        # Read available data
        return self.read_data(size=1024, encoding=encoding)
    
    def get_connection_info(self) -> str:
        """
        Get current connection information.
        
        Returns:
            str: Formatted connection information
        """
        if not self.is_connected:
            return "Not connected to any port"
        
        info = f"Connection Status: Connected\n"
        info += f"Port: {self.current_port}\n"
        info += f"Baud Rate: {self.current_baudrate}\n"
        info += f"Timeout: {self.connection_timeout} seconds\n"
        
        if self.serial_connection:
            info += f"Bytesize: {self.serial_connection.bytesize}\n"
            info += f"Parity: {self.serial_connection.parity}\n"
            info += f"Stop Bits: {self.serial_connection.stopbits}\n"
        
        return info
    
    def is_port_available(self, port: str) -> bool:
        """
        Check if a specific port is available.
        
        Args:
            port (str): Port to check
            
        Returns:
            bool: True if port exists, False otherwise
        """
        ports = serial.tools.list_ports.comports()
        return any(p.device == port for p in ports)
    
    def auto_connect(self, baudrate: int = 9600) -> Optional[str]:
        """
        Try to auto-connect to the first available port.
        
        Args:
            baudrate (int): Baud rate to use
            
        Returns:
            Optional[str]: Connected port name or None if no port found
        """
        ports = serial.tools.list_ports.comports()
        
        if not ports:
            print("No ports available for auto-connect")
            return None
        
        for port in ports:
            print(f"Trying to connect to {port.device}...")
            if self.connect(port.device, baudrate):
                print(f"Successfully connected to {port.device}")
                return port.device
        
        print("Auto-connect failed on all available ports")
        return None

