"""Vectorized weighted sampling and active-leaf inference for two-ply search.

Gumbel top-k is weighted sampling without replacement. It changes the RNG stream,
not the candidate distribution. No opponent parameters or skill heuristics appear.
"""
import numpy as np
import torch
from search_helpers import expand, matrix, terminals


def sample_candidates(priors, masks, width, rng, exploration=0):
    width = min(width, priors.shape[-1])
    legal = masks > 0
    counts = legal.sum(-1, keepdims=True)
    probabilities = (1 - exploration) * priors + exploration * legal / np.maximum(counts, 1)
    keys = np.log(np.maximum(probabilities, 1e-30)) + rng.gumbel(size=priors.shape)
    keys[~legal] = -np.inf
    selected = np.argpartition(-keys, width - 1, axis=-1)[..., :width]
    scores = np.take_along_axis(keys, selected, axis=-1)
    selected = np.take_along_axis(selected, np.argsort(-scores, axis=-1), axis=-1)
    # Preserve ascending enumeration when every legal action fits.
    ordered = np.sort(np.where(legal, np.arange(priors.shape[-1]), priors.shape[-1]), axis=-1)[..., :width]
    selected = np.where(counts <= width, ordered, selected)
    selected[selected == priors.shape[-1]] = -1
    return np.ascontiguousarray(selected, dtype=np.int32)


def candidates(model, observations, masks, width, rng, device, exploration):
    flat = observations.reshape(-1, 718)
    flat_masks = masks.reshape(-1, 156)
    active = np.flatnonzero(flat_masks.any(-1))
    priors = np.zeros_like(flat_masks)
    for start in range(0, len(active), 8192):
        indices = active[start:start+8192]
        logits, _ = model(torch.as_tensor(flat[indices], device=device))
        mask = torch.as_tensor(flat_masks[indices], device=device)
        priors[indices] = logits.masked_fill(mask == 0, -1e9).softmax(-1).cpu().numpy()
    return sample_candidates(priors.reshape(masks.shape), masks, width, rng, exploration)


@torch.no_grad()
def search_tree(model, arena, rng, device, leaf_width=4, iterations=128, exploration=0):
    assert arena.lib.zt_begin() == arena.count
    root_choices = candidates(model,arena.observations,arena.masks,arena.width,rng,device,exploration)
    middle, middle_masks, root_outcomes = expand(arena,root_choices,True)
    leaf_choices = candidates(model,middle,middle_masks,leaf_width,rng,device,exploration)
    leaves, _, leaf_outcomes = expand(arena,leaf_choices,False)
    active = np.flatnonzero(leaf_outcomes.reshape(-1) == 0)
    values = np.zeros((len(leaves),2),np.float32)
    for start in range(0,len(active),4096):
        indices = active[start:start+4096]
        states = leaves[indices].reshape(-1,718)
        _, value = model(torch.as_tensor(states,device=device))
        values[indices] = value.cpu().numpy().reshape(-1,2)
    values = values.reshape(-1,leaf_width,leaf_width,2)
    payoffs = .997 * .5 * (values[...,0]-values[...,1])
    terminals(payoffs,leaf_outcomes)
    _, _, continuation = matrix(payoffs,leaf_choices,iterations)
    payoffs = .997 * continuation.reshape(arena.count,arena.width,arena.width)
    terminals(payoffs,root_outcomes)
    p,q,_ = matrix(payoffs,root_choices,iterations)
    policies = np.zeros((arena.count,2,156),np.float32)
    for game in range(arena.count):
        for seat, distribution in enumerate((p[game],q[game])):
            valid = root_choices[game,seat] >= 0
            policies[game,seat,root_choices[game,seat,valid]] = distribution[valid]
    play = policies.astype(np.float64)
    if exploration:
        play = .97*play + .03*arena.masks/arena.masks.sum(-1,keepdims=True)
    play /= play.sum(-1,keepdims=True)
    u = rng.random(play.shape[:2])[...,None]
    actions = (u > play.cumsum(-1)).sum(-1).astype(np.int32)
    assert (actions < 156).all()
    return actions,policies
