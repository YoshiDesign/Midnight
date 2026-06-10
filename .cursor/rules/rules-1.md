### Rules to live by
- Document a markdown plan before writing a single line of code if you're in agent mode.
- Concurrency structures contain data members which reside on their own cache lines. Do not modify files within the `Runtime/Threading/` directory without strict cache cleanliness.