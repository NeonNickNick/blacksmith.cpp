"""BlacksmithZero v0.1 policy and value network."""
from pathlib import Path

import torch
from torch import nn


class Policy(nn.Module):
    def __init__(self, hidden=256, initialize=True):
        super().__init__()
        self.body = nn.Sequential(
            nn.Linear(718, hidden), nn.Tanh(),
            nn.Linear(hidden, hidden), nn.Tanh(),
            nn.Linear(hidden, hidden), nn.Tanh(),
        )
        self.actor = nn.Linear(hidden, 156)
        self.critic = nn.Linear(hidden, 1)
        if initialize:
            for layer in self.modules():
                if isinstance(layer, nn.Linear):
                    nn.init.orthogonal_(layer.weight, 2 ** .5)
                    nn.init.zeros_(layer.bias)
            nn.init.orthogonal_(self.actor.weight, .01)
            nn.init.zeros_(self.critic.weight)

    def forward(self, observation):
        hidden = self.body(observation)
        return self.actor(hidden), torch.tanh(self.critic(hidden)).squeeze(-1)


def load(path=None):
    checkpoint = Path(path) if path is not None else Path(__file__).with_name("checkpoint.pt")
    data = torch.load(checkpoint, map_location="cpu", weights_only=True)
    model = Policy(hidden=data["hidden"], initialize=False)
    model.load_state_dict(data["model"])
    return model.eval()
