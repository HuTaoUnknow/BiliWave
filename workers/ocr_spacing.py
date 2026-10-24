"""Adjust CTC space decisions using model evidence, without guessing words."""
import math


def spacing_options(config):
    if "ocr_space_probability" in config:
        probability = float(config["ocr_space_probability"])
        if not math.isfinite(probability) or not 0 <= probability <= 1:
            raise ValueError("空格出现概率应为 0 至 100%")
        weight = 0. if probability == 0 else 20. ** (2 * probability - 1)
        return weight, .01
    # Existing submitted tasks keep their original two-parameter interpretation.
    weight = float(config.get("ocr_space_weight", 1.))
    minimum = float(config.get("ocr_space_min_probability", .01))
    if not math.isfinite(weight) or not .1 <= weight <= 20:
        raise ValueError("空格概率倍率应为 0.1 至 20")
    if not math.isfinite(minimum) or not 0 <= minimum <= 1:
        raise ValueError("空格最低概率应为 0 至 100%")
    return weight, minimum


def appearance_probability(config):
    weight, _ = spacing_options(config)
    if "ocr_space_probability" in config:
        return float(config["ocr_space_probability"])
    return max(0., min(1., .5 + .5 * math.log(weight) / math.log(20.)))


class SpaceAwareDecode:
    def __init__(self, decoder, config):
        self.decoder = decoder
        self.weight, self.minimum = spacing_options(config)
        self.space = decoder.character.index(" ")

    def __getattr__(self, name):
        return getattr(self.decoder, name)

    def __call__(self, preds, return_word_box=False, **kwargs):
        if self.weight == 1.:
            return self.decoder(preds, return_word_box, **kwargs)
        import numpy as np
        weighted = preds.copy()
        probability = preds[:, :, self.space]
        weighted[:, :, self.space] = (0. if self.weight == 0 else np.where(
            probability >= self.minimum, probability * self.weight, probability))
        indices = weighted.argmax(axis=2)
        if self.weight == 0:
            # Disabling spaces removes separators instead of replacing them with
            # a runner-up letter. CTC blank keeps repeated letters separated.
            indices = preds.argmax(axis=2)
            indices = np.where(indices == self.space, 0, indices)
        # Confidence remains the original model probability, never the boosted
        # selection score. This also preserves the existing low-confidence filter.
        confidence = np.take_along_axis(preds, indices[:, :, None], axis=2)[:, :, 0]
        result = self.decoder.decode(indices, confidence, return_word_box, is_remove_duplicate=True)
        if return_word_box:
            for index, row in enumerate(result):
                row[2][0] *= kwargs["wh_ratio_list"][index] / kwargs["max_wh_ratio"]
        return result


def apply_spacing(recognizer, config):
    recognizer.postprocess_op = SpaceAwareDecode(recognizer.postprocess_op, config)
