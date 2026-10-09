"""Export a PyTorch checkpoint to the C++ policy.bin format."""
import argparse
from pathlib import Path
import struct
import numpy as np
import torch


def export(checkpoint, output):
    data = torch.load(checkpoint, map_location='cpu', weights_only=True)
    hidden = data['hidden']
    expected = [(718,hidden),(hidden,hidden),(hidden,hidden),(hidden,156),(hidden,1)]
    content = bytearray(struct.pack('<5I',0x31525a42,718,156,hidden,3))
    for name,(inputs,outputs) in zip(['body.0','body.2','body.4','actor','critic'],expected):
        weight, bias = data['model'][name+'.weight'], data['model'][name+'.bias']
        if tuple(weight.shape)!=(outputs,inputs) or tuple(bias.shape)!=(outputs,):
            raise ValueError('Unexpected network shape: '+name)
        for tensor in (weight,bias):
            array = tensor.detach().cpu().numpy().astype('<f4')
            if not np.isfinite(array).all():
                raise ValueError('Nonfinite network parameters')
            content.extend(array.tobytes())
    Path(output).write_bytes(content)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('checkpoint')
    parser.add_argument('output')
    args=parser.parse_args()
    export(args.checkpoint,args.output)
