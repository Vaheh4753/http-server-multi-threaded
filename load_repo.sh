#!/usr/bin/env bash

print_usage() {
    echo "./load_repo.sh {path_to_asgn4_dir} {bin_type}"
}


repo_dir=$1
if [ ! -d $repo_dir ]; then
    echo "$repo_dir does not exist."
    print_usage
    exit 1
fi

bin_dir=bin/$2
if [ ! -d $bin_dir ]; then
    echo "$bin_dir does not exist."
    print_usage
    exit 1
fi

if [ ! -d $repo_dir/test_files ]; then
    mkdir $repo_dir/test_files
fi

if [ ! -d $repo_dir/test_scripts ]; then
    mkdir $repo_dir/test_scripts
fi

if [ ! -d $repo_dir/workloads ]; then
    mkdir $repo_dir/workloads
fi


cp test_scripts/* $repo_dir/test_scripts/
cp test_files/* $repo_dir/test_files/
cp workloads/* $repo_dir/workloads
cp test_repo.sh $repo_dir/
cp $bin_dir/* $repo_dir/
cp *.h $repo_dir/

echo "Tests Loaded!"

exit 0
