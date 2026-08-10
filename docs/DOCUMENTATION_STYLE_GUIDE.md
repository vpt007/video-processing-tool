# Documentation Style Guide

> **Purpose:** Standardized format for writing documentation compatible with the Learniva docs system
> **Version:** 1.0
> **Last Updated:** 2026-08-10

## Table of Contents

1. [Directory Structure](#1-directory-structure)
2. [Index Files](#2-index-files)
3. [File Documentation Format](#3-file-documentation-format)
4. [Function Documentation Tables](#4-function-documentation-tables)
5. [Model Documentation Tables](#5-model-documentation-tables)
6. [Navigation System](#6-navigation-system)
7. [Code References](#7-code-references)
8. [Component Documentation](#8-component-documentation)
9. [API Documentation](#9-api-documentation)
10. [Best Practices](#10-best-practices)
11. [Templates](#11-templates)

---

## 1. Directory Structure

The documentation tree **mirrors the project source tree exactly**. Every source directory must have a corresponding `docs/` directory.

### Rules
- `docs/` directory structure mirrors the project root
- **Every source directory MUST have a corresponding directory under `docs/`**
- **Every docs directory MUST have an `index.md` file**
- Each `index.md` lists all files in that directory
- Complex files get individual `.md` files
- Simple/similar files can be grouped in the directory index

### Directory Structure Example

```
project-root/
├── src/
│   ├── main.c
│   └── ve/
│       └── ve_dsl.c
└── docs/
    ├── src/
    │   ├── index.md
    │   ├── main.md
    │   └── ve/
    │       ├── index.md
    │       └── ve_dsl.md
    └── index.md           ← master index
```

## 2. Index Files

Every directory MUST have an `index.md` file that serves as a table of contents for that directory.

### Index File Template

```markdown
# directory-name/ - Section Title

> **Path:** [`relative/path/`](../relative/path/)
> **Purpose:** Brief description of what this directory contains

## Files

| File | Purpose |
|------|---------|
| [`file1.c`](file1.md) | Description of file1 |
| [`file2.c`](file2.md) | Description of file2 |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Previous Section](../prev/index.md) | [Parent Index](../index.md) | [Next Section](../next/index.md) |
```

### Index Hierarchy
1. **Master index** (`docs/index.md`) - top-level entry point
2. **Section indexes** (`docs/src/index.md`) - overview of major sections
3. **Directory indexes** (`docs/src/ve/index.md`) - file listings for each directory
4. **Individual file docs** (`docs/src/main.md`) - detailed documentation

## 3. File Documentation Format

Each individual file documentation follows this structure:

### File Header

```markdown
# filename.c - Short Description

> **File:** [`path/to/file.c`](../../path/to/file.c)
> **Package:** `packagename`
> **Purpose:** What this file does
```

### Section Organization

1. **Overview** - Brief paragraph explaining the file's role
2. **Global Variables** - Table of package-level variables
3. **Functions** - Detailed function documentation (see §4)
4. **Structs** - For model/type files (see §5)
5. **Environment Variables** - For config files
6. **Navigation** - Previous/Up/Next links

## 4. Function Documentation Tables

Every function must be documented with a table showing its signature, inputs, outputs, and behavior.

### Table Format

```markdown
| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `FunctionName(c)` | `Param1: type, Param2: type` | `StatusCode: {response}` | One-line description |
```

### Detailed Format (for complex functions)

```markdown
### `FunctionName(param1, param2)`
- **Signature:** `func FunctionName(param1 type, param2 type) (returnType, error)`
- **Purpose:** Detailed description of what the function does
- **Input:**
  - `param1` (type) - Description of parameter
- **Output:**
  - Success: `200` → `{field1, field2}`
  - Error: `400` → `{"error": "message"}`
- **Side Effects:** Database writes, cache updates, etc.
- **Called By:** Which routes/endpoints invoke this function
```

### Rules
- **Input column:** Show parameter types, JSON structure, or auth requirements
- **Output column:** Show HTTP status code and response structure
- **Description column:** Explain the business logic, not just "handles X"
- Include error codes and their meanings
- Note side effects

## 5. Model Documentation Tables

Structs use field-level documentation tables.

### Format

```markdown
### StructName
**File:** [`filename.c`](../../path/to/filename.c)

| Field | Type | Tags | Description |
|-------|------|------|-------------|
| `FieldName` | `type` | `gorm tags` | Field description |
```

## 6. Navigation System

Every documentation page MUST have navigation links at the bottom.

### Format

```markdown
## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Previous Page](../prev/page.md) | [Parent Index](../index.md) | [Next Page](../next/page.md) |
```

### Rules
- **← Previous:** Link to the previous page in sequence order
- **↑ Up:** Link to the parent directory index
- **Next →:** Link to the next page in sequence order
- Use relative paths from the doc file's location
- The parent index should always be `../index.md` (one level up) unless the doc is at root level

## 7. Code References

Link to source code files using relative Markdown links with line numbers.

### Format

```markdown
[`FunctionName`](../path/to/file.c:42)
[`filename.c`](../../path/to/file.c)
```

### Rules
- Always use relative paths from the doc file's location
- Include line numbers for specific functions: `file.c:42`
- Include line ranges for multi-line functions: `file.c:20-45`
- Use backticks around code references

### Path Calculation

For a doc at `docs/src/main.md`:
- Source at `src/main.c` → link: `../../src/main.c`
- Which is: `../` goes up from `docs/src/` to `docs/`, then `/src/main.c`

## 8. Component Documentation

UI components should be documented with:

### Format

```markdown
### ComponentName
**File:** [`ComponentName`](../../../../src/path/ComponentName)

| Item | Description |
|------|-------------|
| **Purpose** | What this component does |
| **Props** | `{ prop1: type, prop2: type }` |
| **Features** | Key capabilities |
| **API Calls** | Endpoints it calls |
```

## 9. API Documentation

API endpoints should be documented with:

### Format

```markdown
### METHOD /path/to/endpoint

**Auth:** Required/Not required | **Role:** `role_name`

**Request Body:**
```json
{
  "field1": "value1"
}
```

**Response** `200 OK`:
```json
{
  "field1": "value1"
}
```

**Error Codes:** 400, 401, 403, 404, 500
```

## 10. Best Practices

### Do's
- ✅ **Mirror the source tree** - docs structure matches source structure
- ✅ **Create indexes for ALL directories** - every source dir gets a docs dir with index.md
- ✅ **Use navigation links** - every page has Previous/Up/Next
- ✅ **Reference source files** - link to actual code with line numbers
- ✅ **Document inputs AND outputs** - show structures
- ✅ **Group similar files** - use index files for large directories
- ✅ **Keep descriptions concise** - one paragraph per file
- ✅ **Use tables** - for structured data (fields, functions, config)
- ✅ **Note side effects** - threading, memory, transactions

### Don'ts
- ❌ **Skip navigation** - every page needs Previous/Up/Next
- ❌ **Use absolute paths** - always use relative paths
- ❌ **Leave empty sections** - if a section has no content, omit it
- ❌ **Duplicate content** - reference other docs rather than repeating
- ❌ **Forget edge cases** - document error states and failure modes
- ❌ **Skip directory indexes** - every new directory needs an index.md

### Quality Checklist

#### Structure Checks
- [ ] **Every source directory** has a corresponding docs subdirectory
- [ ] **Every docs subdirectory** has an `index.md` listing all files
- [ ] The **master index** (`docs/index.md`) includes the new section
- [ ] Directory indexes are linked in the parent section's navigation

#### Content Checks
- [ ] Every source file is referenced in at least one doc file
- [ ] Navigation links work (test relative paths)
- [ ] Code references use correct relative paths
- [ ] Function tables include inputs, outputs, and descriptions
- [ ] Model tables include all fields with types and tags
- [ ] No placeholder text or "TODO" comments

## 11. Templates

### Template A: Simple File Documentation
Use for individual utility files, config files, or small modules.

```markdown
# filename.c - Short Description

> **File:** [`path/to/file.c`](../../path/to/file.c)
> **Package:** `packagename`
> **Purpose:** One-line description

## Overview

Brief paragraph explaining the file's purpose and role in the application.

## Global Variables

| Variable | Type | Description |
|----------|------|-------------|
| `VarName` | `string` | Description |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `FuncName(c)` | `param: type` | `status: {response}` | What it does |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Prev](prev.md) | [Index](index.md) | [Next](next.md) |
```

### Template B: Directory Index
Use for directory-level overview pages.

```markdown
# directory-name/ - Section Title

> **Path:** [`relative/source/path/`](../../relative/source/path/)
> **Purpose:** Directory description

## Files

| File | Purpose |
|------|---------|
| [`file1.c`](file1.md) | Description |
| [`file2.c`](file2.md) | Description |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Prev](../prev/index.md) | [Parent](../index.md) | [Next](../next/index.md) |
```

---

## Quick Reference

| Element | Syntax | Example |
|---------|--------|---------|
| File link | `` [`file.c`](path/file.c) `` | [`main.c`](../src/main.md) |
| Function link | `` [`Func()`](path/file.c:42) `` | [`vp_engine_create()`](../src/vp_engine.c:79) |
| Navigation | `` `| ← Previous | Up | Next → |` `` | `| [Config](config.md) | [Index](index.md) | [Routes](routes.md) |` |
| Table | `| H1 | H2 |` | `| Function | Input | Output | Description |` |
| Code block | ```` ```c ```` | ````c int x = 0; ```` |
| Path reference | `` > **Path:** [`path/`](../path/) `` | `> **Path:** [`src/`](../src/)` |