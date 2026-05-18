# Deployment JSON schemas

Machine-readable shapes for campaign / rollout tooling. Used by future `campaign-validate` and documentation.

| Schema | Example data |
|--------|----------------|
| [campaign.schema.json](campaign.schema.json) | `deploy/campaigns/*.json` |
| [rollout_policy.schema.json](rollout_policy.schema.json) | `deploy/rollout_policies/*.json` |
| [campaign_run.schema.json](campaign_run.schema.json) | `deploy/campaign_runs/run_*.json` (future) |

Related package schemas live at `deploy/manifest.schema.json` and `deploy/package_registry.schema.json`.
