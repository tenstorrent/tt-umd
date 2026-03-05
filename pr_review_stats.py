#!/usr/bin/env python3
"""
PR Review Time Stats for tenstorrent/tt-umd
Calculates business days (excluding weekends) from PR creation to first review.
"""

import os
import sys
import json
import urllib.request
import urllib.error
from datetime import datetime, timedelta
from collections import defaultdict


REPO = "tenstorrent/tt-umd"
API_BASE = "https://api.github.com"
PER_PAGE = 100
TOTAL_PRS = 300

FOCUS_USERS = {"pjanevskiTT", "nbuncicTT", "broskoTT", "aleksamarkovicTT"}


def get_token():
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("Warning: GITHUB_TOKEN not set. You may hit rate limits.", file=sys.stderr)
    return token


def gh_request(url, token=None):
    req = urllib.request.Request(url)
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("X-GitHub-Api-Version", "2022-11-28")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read())


def business_days_between(start: datetime, end: datetime) -> float:
    """Count business days (Mon-Fri) between two datetimes, fractional."""
    if end <= start:
        return 0.0

    total_seconds = 0.0
    current = start

    while current < end:
        # Advance to next day boundary or end, whichever is first
        next_day = (current + timedelta(days=1)).replace(
            hour=0, minute=0, second=0, microsecond=0
        )
        chunk_end = min(next_day, end)

        # Only count Mon(0) - Fri(4)
        if current.weekday() < 5:
            total_seconds += (chunk_end - current).total_seconds()

        current = chunk_end

    # Convert seconds to days
    return total_seconds / 86400.0


def fetch_prs(token, states=("open", "closed"), limit=300, author_filter=None):
    """Fetch up to `limit` matching PRs (merged + open), optionally filtered by author set."""
    prs = []
    for state in states:
        page = 1
        while len(prs) < limit:
            url = (
                f"{API_BASE}/repos/{REPO}/pulls"
                f"?state={state}&sort=created&direction=desc"
                f"&per_page={PER_PAGE}&page={page}"
            )
            batch = gh_request(url, token)
            if not batch:
                break

            for pr in batch:
                # For closed PRs, only include merged ones.
                if state == "closed" and not pr.get("merged_at"):
                    continue
                if author_filter and pr["user"]["login"] not in author_filter:
                    continue
                prs.append(pr)
                if len(prs) >= limit:
                    break

            if len(batch) < PER_PAGE or len(prs) >= limit:
                break
            page += 1

    return prs[:limit]


BOT_ACCOUNTS = {"github-actions[bot]", "Copilot"}


def is_bot(login: str) -> bool:
    return login in BOT_ACCOUNTS or login.endswith("[bot]")


def get_review_data(pr_number, pr_author, token):
    """
    Returns (first_review_datetime, set_of_all_human_reviewers).
    Bots and the PR author are excluded from both.
    first_review_datetime is None if no human review found.
    """
    # Map reviewer -> earliest activity datetime, so each person counts once per PR.
    reviewer_times: dict[str, datetime] = {}

    def record(login, dt_str):
        if not login or login == pr_author or is_bot(login):
            return
        dt = datetime.fromisoformat(dt_str.replace("Z", "+00:00"))
        if login not in reviewer_times or dt < reviewer_times[login]:
            reviewer_times[login] = dt

    # 1. PR Reviews (approve, request changes, comment reviews)
    url = f"{API_BASE}/repos/{REPO}/pulls/{pr_number}/reviews?per_page=100"
    try:
        for r in gh_request(url, token):
            record(r.get("user", {}).get("login", ""), r.get("submitted_at") or "")
    except urllib.error.HTTPError:
        pass

    # 2. PR Review Comments (inline diff comments)
    url = f"{API_BASE}/repos/{REPO}/pulls/{pr_number}/comments?per_page=100"
    try:
        for c in gh_request(url, token):
            record(c.get("user", {}).get("login", ""), c.get("created_at") or "")
    except urllib.error.HTTPError:
        pass

    # 3. Issue Comments (general PR comments)
    url = f"{API_BASE}/repos/{REPO}/issues/{pr_number}/comments?per_page=100"
    try:
        for c in gh_request(url, token):
            record(c.get("user", {}).get("login", ""), c.get("created_at") or "")
    except urllib.error.HTTPError:
        pass

    if not reviewer_times:
        return None, {}

    first_review = min(reviewer_times.values())
    return first_review, reviewer_times


def main():
    token = get_token()
    print(f"Fetching last 100 PRs by {', '.join(sorted(FOCUS_USERS))} from {REPO}...")

    # We may need to scan many pages to find 100 PRs from these authors,
    # so pass a high scan limit and filter by author.
    prs = fetch_prs(token, states=["open", "closed"], limit=100, author_filter=FOCUS_USERS)
    print(f"Fetched {len(prs)} PRs. Fetching review data...\n")

    # Per-author stats: list of business days to first review
    author_stats = defaultdict(list)
    # Per-reviewer stats: list of business days from PR creation to their first review activity
    reviewer_response_times = defaultdict(list)
    all_days = []
    no_review_count = 0

    for i, pr in enumerate(prs, 1):
        pr_number = pr["number"]
        pr_author = pr["user"]["login"]
        pr_created_at = datetime.fromisoformat(pr["created_at"].replace("Z", "+00:00"))

        print(f"  [{i:3d}/{len(prs)}] PR #{pr_number} by @{pr_author}", end="", flush=True)

        # Skip bots and anyone outside the focus set.
        if is_bot(pr_author) or pr_author not in FOCUS_USERS:
            print(" — skipped")
            continue

        first_review, reviewer_times = get_review_data(pr_number, pr_author, token)

        for reviewer, review_time in reviewer_times.items():
            if reviewer in FOCUS_USERS:
                reviewer_response_times[reviewer].append(
                    business_days_between(pr_created_at, review_time)
                )

        if first_review is None:
            print(" — no review yet")
            no_review_count += 1
            continue

        days = business_days_between(pr_created_at, first_review)
        author_stats[pr_author].append(days)
        all_days.append(days)
        print(f" — {days:.2f} business days")

    # Show only focus users, sorted by avg wait time.
    all_users = FOCUS_USERS

    # Print results
    print("\n" + "=" * 91)
    print(f"PR FIRST REVIEW TIME STATS — {REPO}")
    print("=" * 91)
    print(f"PRs analyzed: {len(prs)}  |  With reviews: {len(all_days)}  |  No review yet: {no_review_count}")
    print()

    header = (
        f"{'Author':<25} {'PRs authored':>12} {'Avg wait':>10} {'Min':>7} {'Max':>7}"
        f" {'PRs reviewed':>13} {'Avg review time':>16}"
    )
    print(header)
    print("-" * 91)

    def sort_key(user):
        days_list = author_stats.get(user, [])
        return sum(days_list) / len(days_list) if days_list else float("inf")

    for user in sorted(all_users, key=sort_key):
        days_list = author_stats.get(user, [])
        resp_list = reviewer_response_times.get(user, [])
        reviewed = len(resp_list)
        avg_resp = f"{sum(resp_list)/len(resp_list):>16.2f}" if resp_list else f"{'—':>16}"

        if days_list:
            avg = sum(days_list) / len(days_list)
            mn = min(days_list)
            mx = max(days_list)
            print(f"{user:<25} {len(days_list):>12} {avg:>10.2f} {mn:>7.2f} {mx:>7.2f} {reviewed:>13} {avg_resp}")
        else:
            print(f"{user:<25} {'—':>12} {'—':>10} {'—':>7} {'—':>7} {reviewed:>13} {avg_resp}")

    print("-" * 91)
    if all_days:
        overall_avg = sum(all_days) / len(all_days)
        overall_min = min(all_days)
        overall_max = max(all_days)
        all_resp = [d for v in reviewer_response_times.values() for d in v]
        overall_avg_resp = f"{sum(all_resp)/len(all_resp):>16.2f}" if all_resp else f"{'—':>16}"
        print(
            f"{'OVERALL':<25} {len(all_days):>12} {overall_avg:>10.2f} {overall_min:>7.2f} {overall_max:>7.2f}"
            f" {len(all_resp):>13} {overall_avg_resp}"
        )
    print()


if __name__ == "__main__":
    main()
