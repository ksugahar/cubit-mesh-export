# Manual Cubit to COMSOL validation

The `Cubit to COMSOL native validation` workflow is manually dispatched from
the default branch. Use a Windows runner dedicated to this job and labeled
`cubit-comsol-validation`. Restrict the runner to trusted repository code.
Provision Cubit 2026.8, a Python 3.12 site directory with the package's pinned
Netgen dependency, MATLAB with LiveLink, and a licensed local COMSOL server
dedicated to this runner. Run the server with at most eight physical cores.
Start the server as the licensed operator before dispatch. The workflow
expects an unused listener and an empty server model list.

Set these variables in the runner environment:

| Variable | Value |
| --- | --- |
| `COMSOL_CI_CUBIT_ROOT` | Cubit installation directory containing `BUILD_INFO` |
| `COMSOL_CI_PYTHON_SITE` | Python 3.12 site-packages directory with the pinned dependency |
| `COMSOL_CI_EVIDENCE_ROOT` | Private writable evidence directory |
| `COMSOL_CI_MATLAB_EXE` | Absolute path to the licensed MATLAB executable |
| `COMSOL_CI_MLI` | COMSOL LiveLink for MATLAB directory |
| `COMSOL_CI_PORT` | Port of the dedicated local server |

Run `pwsh -File validation_test/comsol/run.ps1` from the checkout to
exercise the same entry point without dispatching the workflow. The
`-ExportOnly` switch checks the Cubit stage without a COMSOL connection.
The script stores the inputs, output models, results and SHA-256 manifest
under a new directory in the configured evidence root.

The fixture has two unit-cross-section regions meeting at x = 0 m. A fixed
end at x = −1 m and a 200 kPa axial traction at x = 1 m give
`u_x = 1e-6*(x+1)` m with E = 200 GPa and Poisson ratio zero. End
temperatures 300 and 302 K give `T = 301+x` K with constant conductivity and
insulated lateral faces. The script checks all stored edge nodes, the shared
faces, and the named selections before comparing the two solutions.
