#!/usr/bin/env bash

# Number of parallel executions
parallelism=2

# Number of initial elements on the list
export init_elt=20
export mod_file="/proc/modlist"

read_from_list() {
    local file_contents=$(cat "${mod_file}")
    echo "${file_contents}"
}
# Export so can use on subshells (same for the rest)
export -f read_from_list

add_to_list() {
    local input="${1:-10}"
    echo "add ${input}" > "${mod_file}"
}
export -f add_to_list

remove_from_list() {
    local input="${1:-10}"
    echo "remove ${input}" > "${mod_file}"
}
export -f remove_from_list

cleanup_list() {
    echo "cleanup" > "${mod_file}"
}
export -f cleanup_list

# Reset list to init contents
clean_and_add() {
    cleanup_list
    for i in $(seq 1 ${init_elt}); do
        add_to_list $i > /dev/null
    done
}

# Read in parallel
read_read_conflict() {
    seq 1 "${parallelism}" | xargs -n 1 -P "${parallelism}" bash -c 'read_from_list "@"' _
}


# Add two different elements in parallel
add_add_conflict() {
    seq 1 "${parallelism}" | xargs -n 1 -P "${parallelism}" bash -c 'add_to_list "$@"' _
    read_from_list
}


# Parallel add / remove
add_rm_conflict() {
    add_to_list 20
    add_to_list 20 &
    remove_from_list 20 &
    wait
    read_from_list
}

# Parallel removes
rm_rm_conflict() {
    seq 1 "${parallelism}" | xargs -n 1 -P "${parallelism}" bash -c 'add_to_list "$@"' _
    seq 1 "${parallelism}" | xargs -n 1 -P "${parallelism}" bash -c 'remove_from_list "$@"' _
    read_from_list
}

# Parallel add and cleanup
add_cleanup_conflict() {
    add_to_list 90 &
    cleanup_list &
    wait
    read_from_list
}

# Parallel remove and cleanup
rm_cleanup_conflict() {
    add_to_list 10
    remove_from_list 10 &
    cleanup_list &
    wait
    read_from_list
}

main() {
    if [[ ! -a "${mod_file}" ]]; then
        echo "Module not loaded, aborting..."
        exit -1
    fi

    clean_and_add

    echo "read_read_conflict, should be same contents"
    read_read_conflict

    cleanup_list

    echo "add_add_conflict, should add both, whatever order"
    add_add_conflict

    cleanup_list

    echo "rm_rm_conflict, should remove both"
    rm_rm_conflict

    cleanup_list

    echo "add_rm_conflict, should either leave one or zero elements"
    add_rm_conflict

    clean_and_add

    echo "add_cleanup_conflict, should either cleanup the list or add a single item"
    add_cleanup_conflict

    clean_and_add

    echo "rm_cleanup_conflict, should leave an empty list"
    rm_cleanup_conflict

    cleanup_list
}

main "$@"
