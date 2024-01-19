#!/bin/bash

# Exit on error. Append "|| true" if you expect an error.
set -o errexit
# Exit on error inside any functions or subshells.
set -o errtrace
# Do not allow use of undefined vars. Use ${VAR:-} to use an undefined VAR
set -o nounset
# Catch the error in case mysqldump fails (but gzip succeeds) in `mysqldump |gzip`
set -o pipefail
# Turn on traces, useful while debugging but commented out by default
# set -o xtrace

branches=(
  assignment_1
  assignment_2
  assignment_3
  assignment_4
  assignment_5
  assignment_7
  assignment_8
  assignment_9
  assignment_10
  assignment_11
  assignment_2_solution
  assignment_3_solution
  assignment_4_solution
  assignment_5_solution
  assignment_7_solution
  assignment_8_solution
  assignment_9_solution
  assignment_10_solution
  assignment_11_solution
)

cherry_pick()
{
  echo "Cherry picking $1 into all branches"

  for i in "${branches[@]}"; do
    echo "Cherry picking into $i"
    # git pull origin $i
    git checkout $i
    if git cherry-pick $1 ; then
      echo "Cherry pick succeeded"
    else
      echo "Cherry pick failed"
      git cherry-pick --abort
    fi
  done
  git checkout main
}

push_all()
{
  echo "Pushing all branches"

  for i in "${branches[@]}"; do
    echo "Pushing $i"
    git push origin $i
  done
}

fetch_all()
{
  echo "Fetching all branches"

  for i in "${branches[@]}"; do
    echo "Fetch $i"
    git fetch origin $i
  done
}

while getopts ":c:p:f" option; do
  case $option in
    c) # cherry-pick
      cherry_pick "$OPTARG"
      exit;;
    p) # push all
      push_all
      exit;;
    f) # fetch all
      fetch_all
      exit;;
    \?) # Invalid option
         echo "Error: Invalid option"
         exit;;
  esac
done
