# Graph Database Implementation for Social Media Network

## Overview

This project implements a graph database designed to represent and manage a social media network. The graph consists of **users** and their relationships, represented as nodes and edges, respectively. Each user can also create **posts** with associated metadata.

## How to Run the Program

How to Run the Program
1.	Ensure you have a compatible C++17 compiler installed (e.g., GCC or Clang).
2.	Copy the program code into a file named buzzdb_ec.cpp.
3.	Open a terminal and navigate to the directory containing buzzdb_ec.cpp.
4.	Compile and run the program using the following command:
```bash
g++ -fdiagnostics-color -std=c++17 -O3 -Wall -Werror -Wextra buzzdb_ec.cpp -o a.out
```
5. This creates the executable. Run the following command to run the program:
```bash
./a.out
```
6. You should be greeted with the following text:
```bash
Choose an option:
1. Run all unit tests
2. Find nth-degree connections
   (Find all nodes within a specified degree of connection to a given person)
3. Find connections and likes
   (Find the number of colleagues and friends a user has, along with their likes)
4. Exit
Enter your choice:
```
- Options 2 and 3 allow to input a name and degree and also showcase the execution time for the query.

### Social Media Network Details
- **Users**:
  - Attributes:
    - `name`: The name of the user (e.g., "Alice").
    - `type`: Indicates the entity type (always "user" for users).
    - `age`: The age of the user.
  - Example: `{"name": "Alice", "type": "user", "age": 25}`
- **Posts**:
  - Attributes:
    - `title`: The title of the post.
    - `type`: Indicates the entity type (always "post" for posts).
    - `likes`: The number of likes on the post.
  - Example: `{"title": "Vacation", "type": "post", "likes": 10}`
- **Edges (Relationships)**:
  - Represent connections between users.
  - Attributes:
    - `relationship`: The type of relationship (e.g., "friend" or "colleague").

---

## Functions

### **1. Find nth-Degree Connections**
#### Description:
This function identifies all the nodes that are within a specified degree of connection to a given user.

#### Implementation Details:
- Input: 
  - `user_id`: The node ID of the starting user.
  - `degree`: The degree of connection to find.
- Steps:
  1. Use a breadth-first search (BFS) algorithm.
  2. Maintain a queue of nodes to explore, tracking their current degree.
  3. Explore neighbors and add them to the result if they match the required degree.
  4. Filter the result to include only nodes of type "user."
- Output:
  - A list of user nodes within the specified degree of connection.

---

### **2. Find Connections and Likes**
#### Description:
This function calculates the number of colleagues and friends a user has and aggregates the total number of likes for each connection.

#### Implementation Details:
- Input: 
  - `user_id`: The node ID of the user for whom connections and likes are to be calculated.
- Steps:
  1. Traverse the adjacency matrix to identify direct neighbors (connections).
  2. For each neighbor:
     - Check the relationship type (`friend` or `colleague`).
     - Count the total number of "likes" across all posts created by the neighbor.
  3. Return a map with two keys:
     - `colleagues`: List of colleagues and their total likes.
     - `friends`: List of friends and their total likes.
- Output:
  - A map with two lists: `colleagues` and `friends`.