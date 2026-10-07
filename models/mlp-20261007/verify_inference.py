"""Independent NumPy/C++ parity check; synthetic inputs, no game records."""
import argparse
import hashlib
import pathlib
import struct
import subprocess

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=pathlib.Path)
args = parser.parse_args()
model = pathlib.Path(__file__).with_name("policy.bin")
data = model.read_bytes()
assert hashlib.sha256(data).hexdigest() == "924622d638409435a8bc878ee2a94ef1c71bae4a013849dceba81a698102249c"
magic, features, actions, hidden, depth = struct.unpack_from("<5I", data)
assert (magic, features, actions, hidden, depth) == (0x314e5342, 718, 156, 512, 3)
offset = 20
layers = []
for inputs, outputs in [(718, 512), (512, 512), (512, 512), (512, 156)]:
    weights = np.frombuffer(data, "<f4", inputs * outputs, offset).reshape(outputs, inputs)
    offset += 4 * inputs * outputs
    bias = np.frombuffer(data, "<f4", outputs, offset)
    offset += 4 * outputs
    layers.append((weights, bias))
assert offset == len(data)
rng = np.random.default_rng(20261008)
maximum_error = 0.0
for case in range(8):
    observation = rng.uniform(-1, 1, features).astype(np.float32)
    values = observation
    for index, (weight, bias) in enumerate(layers):
        values = weight @ values + bias
        if index < 3:
            values = np.tanh(values)
    mask = np.zeros(actions, np.float32)
    only_legal = case * 19
    mask[only_legal] = 1
    text = " ".join(map(str, np.concatenate([observation, mask])))
    result = subprocess.run([str(args.executable.resolve()), str(model), "--logits"],
                            input=text, capture_output=True, text=True, check=True)
    actual = np.fromstring(result.stdout, sep=" ")
    np.testing.assert_allclose(actual, values, atol=2e-4, rtol=2e-4)
    maximum_error = max(maximum_error, float(np.max(np.abs(actual - values))))
    result = subprocess.run([str(args.executable.resolve()), str(model)],
                            input=text, capture_output=True, text=True, check=True)
    assert int(result.stdout) == only_legal
print(f"PASS cases=8 max_logit_absolute_error={maximum_error:.8g} legal_mask_cases=8")
