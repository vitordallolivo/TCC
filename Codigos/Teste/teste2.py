import numpy as np
import time

# Simula carga do observador por 1 segundo a 20kHz
A = np.array([[0, 1], [-2, -3]], dtype=np.float32)
x = np.array([0.0, 0.0], dtype=np.float32)

start = time.perf_counter()
for _ in range(20000):
    x = A @ x   # operação mais pesada do observador
elapsed = time.perf_counter() - start

print(f"20000 iterações em {elapsed*1000:.1f}ms")
print(f"{'OK — Python resolve' if elapsed < 1.0 else 'LENTO — precisa de hardware'}")