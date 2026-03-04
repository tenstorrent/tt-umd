#!/usr/bin/env python3
"""
Analyze PR response times for users in tt-umd repository.
This script calculates how fast each user responds to PRs.
"""

import requests
import sys
from datetime import datetime
from collections import defaultdict
from typing import Dict, List, Tuple


def get_github_token():
    """Try to get GitHub token from environment or git config."""
    import os
    import subprocess
    
    # Try environment variable first
    token = os.environ.get('GITHUB_TOKEN')
    if token:
        return token
    
    # Try gh CLI config
    try:
        result = subprocess.run(
            ['gh', 'auth', 'token'],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass
    
    return None


def fetch_prs(repo: str, state: str = 'all', per_page: int = 100) -> List[dict]:
    """Fetch PRs from GitHub API."""
    token = get_github_token()
    headers = {'Accept': 'application/vnd.github.v3+json'}
    if token:
        headers['Authorization'] = f'token {token}'
    
    all_prs = []
    page = 1
    
    while True:
        url = f'https://api.github.com/repos/{repo}/pulls'
        params = {
            'state': state,
            'per_page': per_page,
            'page': page,
            'sort': 'updated',
            'direction': 'desc'
        }
        
        response = requests.get(url, headers=headers, params=params)
        
        if response.status_code != 200:
            print(f"Error fetching PRs: {response.status_code}")
            print(f"Message: {response.json().get('message', 'Unknown error')}")
            sys.exit(1)
        
        prs = response.json()
        if not prs:
            break
        
        all_prs.extend(prs)
        print(f"Fetched {len(all_prs)} PRs so far...", file=sys.stderr)
        
        # Stop after a reasonable number of PRs
        if len(all_prs) >= 500:
            break
        
        page += 1
    
    return all_prs


def fetch_pr_comments(repo: str, pr_number: int) -> List[dict]:
    """Fetch all comments for a PR (both review comments and issue comments)."""
    token = get_github_token()
    headers = {'Accept': 'application/vnd.github.v3+json'}
    if token:
        headers['Authorization'] = f'token {token}'
    
    all_comments = []
    
    # Fetch issue comments (general PR comments)
    url = f'https://api.github.com/repos/{repo}/issues/{pr_number}/comments'
    response = requests.get(url, headers=headers)
    if response.status_code == 200:
        all_comments.extend(response.json())
    
    # Fetch review comments (code review comments)
    url = f'https://api.github.com/repos/{repo}/pulls/{pr_number}/comments'
    response = requests.get(url, headers=headers)
    if response.status_code == 200:
        all_comments.extend(response.json())
    
    # Fetch review submissions (includes approvals without comments)
    url = f'https://api.github.com/repos/{repo}/pulls/{pr_number}/reviews'
    response = requests.get(url, headers=headers)
    if response.status_code == 200:
        reviews = response.json()
        # Convert reviews to comment-like format
        # Include all reviews (APPROVED, CHANGES_REQUESTED, COMMENTED)
        for review in reviews:
            # Skip PENDING or DISMISSED reviews
            if review.get('state') in ['APPROVED', 'CHANGES_REQUESTED', 'COMMENTED']:
                all_comments.append({
                    'user': review['user'],
                    'created_at': review['submitted_at'],
                    'body': review.get('body', ''),
                    'state': review['state']
                })
    
    return all_comments


def calculate_response_times(repo: str) -> Dict[str, List[float]]:
    """Calculate response times for each user."""
    print("Fetching PRs...", file=sys.stderr)
    prs = fetch_prs(repo)
    
    user_response_times = defaultdict(list)
    
    for i, pr in enumerate(prs):
        pr_number = pr['number']
        pr_author = pr['user']['login']
        pr_created_at = datetime.strptime(pr['created_at'], '%Y-%m-%dT%H:%M:%SZ')
        
        print(f"Analyzing PR #{pr_number} ({i+1}/{len(prs)})...", file=sys.stderr)
        
        comments = fetch_pr_comments(repo, pr_number)
        
        if not comments:
            continue
        
        # Sort comments by time
        comments.sort(key=lambda c: c['created_at'])
        
        # Find first response from someone other than PR author
        for comment in comments:
            commenter = comment['user']['login']
            if commenter != pr_author:
                comment_time = datetime.strptime(comment['created_at'], '%Y-%m-%dT%H:%M:%SZ')
                response_time_hours = (comment_time - pr_created_at).total_seconds() / 3600
                
                user_response_times[commenter].append(response_time_hours)
                break  # Only count first response
    
    return user_response_times


def format_time(hours: float) -> str:
    """Format hours into a readable string."""
    if hours < 1:
        return f"{hours * 60:.1f} minutes"
    elif hours < 24:
        return f"{hours:.1f} hours"
    else:
        days = hours / 24
        return f"{days:.1f} days"


def main():
    repo = 'tenstorrent/tt-umd'
    
    print(f"Analyzing PR response times for {repo}...\n", file=sys.stderr)
    
    user_response_times = calculate_response_times(repo)
    
    # Calculate statistics
    user_stats = []
    for user, times in user_response_times.items():
        if not times:
            continue
        
        avg_time = sum(times) / len(times)
        median_time = sorted(times)[len(times) // 2]
        min_time = min(times)
        max_time = max(times)
        
        user_stats.append({
            'user': user,
            'avg': avg_time,
            'median': median_time,
            'min': min_time,
            'max': max_time,
            'count': len(times)
        })
    
    # Sort by average response time
    user_stats.sort(key=lambda x: x['avg'])
    
    # Print results
    print("\n" + "="*80)
    print("PR Response Time Analysis for tt-umd")
    print("="*80)
    print(f"\n{'User':<20} {'Avg Response':<15} {'Median':<15} {'Min':<15} {'Max':<15} {'# PRs':<10}")
    print("-"*95)
    
    for stat in user_stats:
        print(f"{stat['user']:<20} "
              f"{format_time(stat['avg']):<15} "
              f"{format_time(stat['median']):<15} "
              f"{format_time(stat['min']):<15} "
              f"{format_time(stat['max']):<15} "
              f"{stat['count']:<10}")
    
    print("\n" + "="*80)
    print(f"Analyzed {len(user_stats)} users who responded to PRs")
    print("="*80)


if __name__ == '__main__':
    main()
