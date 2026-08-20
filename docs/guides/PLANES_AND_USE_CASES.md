# Planes Explained — Examples and Use Cases

This document explains each plane in the multi-plane model and how to choose one for a request.

![Plane routing](../images/plane-routing.svg)

---

## 1) Plane summary table

| Plane | Purpose | Typical owner | Risk profile | Example tools |
|---|---|---|---|---|
| `triage` | Read-only diagnostics and debugging | SRE / Support | Low-to-medium | uptime, memory, interface status |
| `management` | Operational management actions | Device/platform ops | Medium | service control, operational snapshots |
| `control` | Runtime control and policy toggles | Platform control-plane | Medium-to-high | mode toggles, policy refresh |
| `config-apply` | Applying configuration changes | Config orchestrator | High | apply/commit/rollback config tasks |

---

## 2) Triage plane

### What it is
Focused on observability and debugging data retrieval.

### Good examples
- Read process state
- Read network status
- Read counters/log subsets

### Cloud request example

```json
{
  "tool": "device_uptime",
  "plane": "triage"
}
```

### Scenario
A support workflow receives degraded-performance alarms and triggers a triage bundle across impacted devices.

### Advantage
- Lowest blast radius for cloud diagnostics
- Best default plane for broad fleet observability
- Simplifies support workflows by keeping actions mostly read-only

---

## 3) Management plane

### What it is
Operational management plane for routine lifecycle and service management tasks.

### Good examples
- Controlled service status checks
- Non-disruptive maintenance commands
- Device operational metadata collection

### Cloud request example

```json
{
  "tool": "service_inventory",
  "plane": "management"
}
```

### Scenario
A nightly health campaign checks service inventory consistency before patch windows.

### Advantage
- Operational automation can run without entering high-risk config channels
- Better governance separation between diagnostics and operations
- Reduces accidental mutation from routine jobs

---

## 4) Control plane

### What it is
Control path for runtime behavior toggles and orchestrator-issued control actions.

### Good examples
- Feature mode toggles
- Runtime policy reload
- Dynamic behavior switch commands

### Cloud request example

```json
{
  "tool": "reload_runtime_policy",
  "plane": "control"
}
```

### Scenario
Cloud control turns on extra telemetry for a region during incident response.

### Advantage
- Supports fast runtime steering during incidents
- Keeps control toggles separate from persistent configuration workflows
- Enables policy-driven change windows for runtime behavior

---

## 5) Config-apply plane

### What it is
High-impact configuration apply path.

### Good examples
- Apply candidate config
- Validate + commit config
- Rollback on failure

### Cloud request example

```json
{
  "tool": "apply_candidate_profile",
  "plane": "config-apply"
}
```

### Scenario
A staged rollout applies a new network profile to 5% canary devices, validates, then expands.

### Advantage
- Explicit high-risk channel for mutable state
- Easier compliance/audit on configuration-changing operations
- Clear rollback-oriented operational process

---

## 5.1 Cross-plane scenario examples

### Scenario A: Incident triage to controlled mitigation
1. `triage` collects memory/network/process evidence.
2. `control` enables temporary mitigation mode.
3. `management` verifies service health after mitigation.
4. `config-apply` commits persistent fix if approved.

### Scenario B: Safe feature rollout
1. `management` checks prerequisite services.
2. `control` toggles runtime feature for canary cohort.
3. `triage` validates telemetry and error rates.
4. `config-apply` persists rollout config after success criteria.

### Scenario C: Compliance and audit-friendly operations
1. `triage` gathers evidence snapshots.
2. `management` executes routine operational checks.
3. `config-apply` performs approved config mutation with versioned records.

---

## 6) Plane design principles

1. **Isolation by intent**
   Keep diagnostics separate from config mutation.

2. **Least privilege by plane**
   ACL and operator permissions should differ by plane.

3. **Clear blast-radius boundaries**
   `config-apply` actions should have stronger governance than `triage`.

4. **Explicit cloud targeting**
   Always include `plane` in requests for deterministic routing.

---

## 7) Catalog and plugin implications

- Each plane has its own catalog file and version history.
- Plugin directories can be configured per plane for stronger operational isolation.
- Tool name collisions across planes require explicit `plane` from client side to avoid ambiguity.

---

## 8) Anti-patterns

- Mixing mutating tools into `triage`
- Omitting `plane` in cloud requests when tool names overlap
- Using dynamic command overrides where static pinned tools are expected
