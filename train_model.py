# train_model.py
#
# Trains the micro-sleep decision model, and exports it.

import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix

# 1. Load data.
#   train_baseline.csv          : Poisson + MMPP burst/quiet traffic  (TRAIN)
#   train_heavytail_diurnal.csv : self-similar (Pareto) + diurnal     (HELD-OUT)
# Each row is one empty-poll decision point: five features + the oracle's
# best sleep duration in microseconds.
training_data = pd.read_csv("train_baseline.csv")
heldout_data  = pd.read_csv("train_heavytail_diurnal.csv")

# The five features the model is allowed to see at each empty poll.
FEATURE_COLUMNS = [
    "empty_run",      # consecutive empty polls so far
    "us_since_pkt",   # microseconds since the last non-empty poll
    "rate_fast",      # EWMA of packets/poll, alpha=0.20  (~5  polls of memory)
    "rate_med",       # EWMA of packets/poll, alpha=0.02  (~50 polls of memory)
    "rate_slow",      # EWMA of packets/poll, alpha=0.002 (~500 polls of memory)
]

# 2. Build the binary label from the multi-duration oracle label.
#   best_sleep > 0  ->  the oracle wanted SOME sleep  ->  class 1 ("sleep")
#   best_sleep == 0 ->  the oracle wanted to keep polling -> class 0 ("poll")
for frame in (training_data, heldout_data):
    frame["sleep_decision"] = (frame["best_sleep"] > 0).astype(int)

LABEL_COLUMN = "sleep_decision"

# The majority-class fraction is the bar any useful model must beat.
majority_fraction = training_data[LABEL_COLUMN].value_counts(normalize=True).max()
print(f"majority-class baseline accuracy to beat: {majority_fraction:.3f}")
print(training_data[LABEL_COLUMN].value_counts(normalize=True).sort_index())

# 3. Train / test split on the baseline traffic.
#   Stratify keeps the class ratio identical in both halves.
features = training_data[FEATURE_COLUMNS]
labels   = training_data[LABEL_COLUMN]

features_train, features_test, labels_train, labels_test = train_test_split(
    features, labels, test_size=0.25, random_state=0, stratify=labels)

# 4. Decision tree
#   max_depth=6 keeps inference cheap (a short cascade of comparisons, which
#   matters in a per-poll fast path) and keeps the learned rule interpretable.
#   Tried class_weight="balanced" but that pushed the model below the majority
#   baseline by over-chasing the rare class. More details in report.
decision_tree = DecisionTreeClassifier(max_depth=6, random_state=0)
decision_tree.fit(features_train, labels_train)

tree_test_predictions = decision_tree.predict(features_test)
print(f"\n[decision tree] test accuracy: "
      f"{accuracy_score(labels_test, tree_test_predictions):.3f}")
print(classification_report(labels_test, tree_test_predictions,
                            digits=3, zero_division=0))

print("feature importances (which signals the tree actually relies on):")
for column_name, importance in zip(FEATURE_COLUMNS, decision_tree.feature_importances_):
    print(f"  {column_name:14s} {importance:.3f}")

print("\ntop of the tree (the human-readable learned rule):")
print(export_text(decision_tree, feature_names=FEATURE_COLUMNS, max_depth=2))

confusion = confusion_matrix(labels_test, tree_test_predictions, labels=[0, 1])
print("confusion matrix  [rows = true, cols = predicted]   0=poll  1=sleep")
print(f"            pred_poll  pred_sleep")
print(f"true_poll   {confusion[0][0]:>9}  {confusion[0][1]:>10}")
print(f"true_sleep  {confusion[1][0]:>9}  {confusion[1][1]:>10}")

# 5. Generalization test on traffic the model never trained on
#   (self-similar bursts + diurnal load).
heldout_features = heldout_data[FEATURE_COLUMNS]
heldout_labels   = heldout_data[LABEL_COLUMN]
print(f"\n[decision tree] held-out (heavy-tail + diurnal) accuracy: "
      f"{decision_tree.score(heldout_features, heldout_labels):.3f}")

# 6. Logistic regression
#  Also a cheap model to try out.
logistic_model = LogisticRegression(max_iter=5000)
logistic_model.fit(features_train, labels_train)
print(f"\n[logistic regression] test accuracy: "
      f"{accuracy_score(labels_test, logistic_model.predict(features_test)):.3f}")

# 7. Export the decision tree to C.
#   m2cgen emits a self-contained score() function (no runtime dependency).
#   Discarded logistic regression since decision tree outperforms it.
import m2cgen
exported_c_source = m2cgen.export_to_c(decision_tree)
with open("model_exported.c", "w") as output_file:
    output_file.write(exported_c_source)

print("\nexported C model written to model_exported.c")
print("first lines of the exported function:")
print("\n".join(exported_c_source.splitlines()[:20]))
