#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <map>
#include <vector>
#include <list>
#include <unordered_map>
#include <string>
#include <memory>
#include <sstream>
#include <limits>
#include <thread>
#include <queue>
#include <optional>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <cassert>
#include <cstring> 
#include <exception>
#include <atomic>
#include <set>
#include <variant>
#include <stdexcept>
#include <unordered_set>

#define UNUSED(p)  ((void)(p))

#define RESET "\033[0m"
#define PROMPT_COLOR "\033[1;34m" // Bright blue for prompts
#define RESULT_COLOR "\033[1;32m" // Bright green for results

#define ASSERT_WITH_MESSAGE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "Assertion \033[1;31mFAILED\033[0m: " << message << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while(0)

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;
    std::unique_ptr<char[]> data;
    size_t data_length;

public:
    Field(int i) : type(INT) { 
        data_length = sizeof(int);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &i, data_length);
    }

    Field(float f) : type(FLOAT) { 
        data_length = sizeof(float);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &f, data_length);
    }

    Field(const std::string& s) : type(STRING) {
        data_length = s.size() + 1;  // include null-terminator
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), s.c_str(), data_length);
    }

    Field& operator=(const Field& other) {
        if (&other == this) {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field(Field&& other){
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
    }

    FieldType getType() const { return type; }
    int asInt() const { 
        return *reinterpret_cast<int*>(data.get());
    }
    float asFloat() const { 
        return *reinterpret_cast<float*>(data.get());
    }
    std::string asString() const { 
        return std::string(data.get());
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << type << ' ' << data_length << ' ';
        if (type == STRING) {
            buffer << data.get() << ' ';
        } else if (type == INT) {
            buffer << *reinterpret_cast<int*>(data.get()) << ' ';
        } else if (type == FLOAT) {
            buffer << *reinterpret_cast<float*>(data.get()) << ' ';
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Field> deserialize(std::istream& in) {
        int type; in >> type;
        size_t length; in >> length;
        if (type == STRING) {
            std::string val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == INT) {
            int val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == FLOAT) {
            float val; in >> val;
            return std::make_unique<Field>(val);
        }
        return nullptr;
    }

    void print() const{
        switch(getType()){
            case INT: std::cout << asInt(); break;
            case FLOAT: std::cout << asFloat(); break;
            case STRING: std::cout << asString(); break;
        }
    }
};

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    size_t getSize() const {
        size_t size = 0;
        for (const auto& field : fields) {
            size += field->data_length;
        }
        return size;
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << fields.size() << ' ';
        for (const auto& field : fields) {
            buffer << field->serialize();
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Tuple> deserialize(std::istream& in) {
        auto tuple = std::make_unique<Tuple>();
        size_t fieldCount; in >> fieldCount;
        for (size_t i = 0; i < fieldCount; ++i) {
            tuple->addField(Field::deserialize(in));
        }
        return tuple;
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
// static constexpr size_t PAGE_SIZE = 32408;  // Fixed page size
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
static constexpr size_t MAX_PAGES= 1000;   // Total Number of pages that can be stored
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS;

    SlottedPage(){
        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }
    }

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple) {

        // Serialize the tuple into a char array
        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();

        //std::cout << "Tuple size: " << tuple_size << " bytes\n";
        assert(tuple_size == 38);

        // Check for first slot with enough space
        size_t slot_itr = 0;
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());        
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == true and 
                slot_array[slot_itr].length >= tuple_size) {
                break;
            }
        }
        if (slot_itr == MAX_SLOTS){
            //std::cout << "Page does not contain an empty slot with sufficient space to store the tuple.";
            return false;
        }

        // Identify the offset where the tuple will be placed in the page
        // Update slot meta-data if needed
        slot_array[slot_itr].empty = false;
        size_t offset = INVALID_VALUE;
        if (slot_array[slot_itr].offset == INVALID_VALUE){
            if(slot_itr != 0){
                auto prev_slot_offset = slot_array[slot_itr - 1].offset;
                auto prev_slot_length = slot_array[slot_itr - 1].length;
                offset = prev_slot_offset + prev_slot_length;
            }
            else{
                offset = metadata_size;
            }

            slot_array[slot_itr].offset = offset;
        }
        else{
            offset = slot_array[slot_itr].offset;
        }

        if(offset + tuple_size >= PAGE_SIZE){
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset + tuple_size < PAGE_SIZE);

        if (slot_array[slot_itr].length == INVALID_VALUE){
            slot_array[slot_itr].length = tuple_size;
        }

        // Copy serialized data into the page
        std::memcpy(page_data.get() + offset, 
                    serializedTuple.c_str(), 
                    tuple_size);

        return true;
    }

    void deleteTuple(size_t index) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        size_t slot_itr = 0;
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if(slot_itr == index and
               slot_array[slot_itr].empty == false){
                slot_array[slot_itr].empty = true;
                break;
               }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void print() const{
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == false){
                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_data.get() + slot_array[slot_itr].offset;
                std::istringstream iss(tuple_data);
                auto loadedTuple = Tuple::deserialize(iss);
                std::cout << "Slot " << slot_itr << " : [";
                std::cout << (uint16_t)(slot_array[slot_itr].offset) << "] :: ";
                loadedTuple->print();
            }
        }
        std::cout << "\n";
    }
};

const std::string database_filename = "buzzdb.dat";

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;
    std::mutex io_mutex;

public:
    StorageManager(bool truncate_mode = true){
        auto flags =  truncate_mode ? std::ios::in | std::ios::out | std::ios::trunc 
            : std::ios::in | std::ios::out;
        fileStream.open(database_filename, flags);
        if (!fileStream) {
            // If file does not exist, create it
            fileStream.clear(); // Reset the state
            fileStream.open(database_filename, truncate_mode ? (std::ios::out | std::ios::trunc) : std::ios::out);
        }
        fileStream.close(); 
        fileStream.open(database_filename, std::ios::in | std::ios::out); 

        fileStream.seekg(0, std::ios::end);
        num_pages = fileStream.tellg() / PAGE_SIZE;

        if(num_pages == 0){
            extend();
        }

    }

    ~StorageManager() {
        if (fileStream.is_open()) {
            fileStream.close();
        }
    }

    // Read a page from disk
    std::unique_ptr<SlottedPage> load(uint16_t page_id) {
        fileStream.seekg(page_id * PAGE_SIZE, std::ios::beg);
        auto page = std::make_unique<SlottedPage>();
        // Read the content of the file into the page
        if(fileStream.read(page->page_data.get(), PAGE_SIZE)){
            //std::cout << "Page read successfully from file." << std::endl;
        }
        else{
            std::cerr << "Error: Unable to read data from the file. \n";
            exit(-1);
        }
        return page;
    }

    // Write a page to disk
    void flush(uint16_t page_id, const SlottedPage& page) {
        size_t page_offset = page_id * PAGE_SIZE;        

        // Move the write pointer
        fileStream.seekp(page_offset, std::ios::beg);
        fileStream.write(page.page_data.get(), PAGE_SIZE);        
        fileStream.flush();
    }

    // Extend database file by one page
    void extend() {
        // Create a slotted page
        auto empty_slotted_page = std::make_unique<SlottedPage>();

        // Move the write pointer
        fileStream.seekp(0, std::ios::end);

        // Write the page to the file, extending it
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        fileStream.flush();

        // Update number of pages
        num_pages += 1;
    }

    void extend(uint64_t till_page_id) {
        std::lock_guard<std::mutex>  io_guard(io_mutex); 
        uint64_t write_size = std::max(static_cast<uint64_t>(0), till_page_id + 1 - num_pages) * PAGE_SIZE;
        if(write_size > 0 ) {
            // std::cout << "Extending database file till page id : "<<till_page_id<<" \n";
            char* buffer = new char[write_size];
            std::memset(buffer, 0, write_size);

            fileStream.seekp(0, std::ios::end);
            fileStream.write(buffer, write_size);
            fileStream.flush();
            
            num_pages = till_page_id+1;
        }
    }

};

using PageID = uint16_t;

class Policy {
public:
    virtual bool touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;
};

void printList(std::string list_name, const std::list<PageID>& myList) {
        std::cout << list_name << " :: ";
        for (const PageID& value : myList) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
}

class LruPolicy : public Policy {
private:
    // List to keep track of the order of use
    std::list<PageID> lruList;

    std::unordered_map<PageID, std::list<PageID>::iterator> map;

    size_t cacheSize;

public:

    LruPolicy(size_t cacheSize) : cacheSize(cacheSize) {}

    bool touch(PageID page_id) override {
        //printList("LRU", lruList);

        bool found = false;
        // If page already in the list, remove it
        if (map.find(page_id) != map.end()) {
            found = true;
            lruList.erase(map[page_id]);
            map.erase(page_id);            
        }

        // If cache is full, evict
        if(lruList.size() == cacheSize){
            evict();
        }

        if(lruList.size() < cacheSize){
            // Add the page to the front of the list
            lruList.emplace_front(page_id);
            map[page_id] = lruList.begin();
        }

        return found;
    }

    PageID evict() override {
        // Evict the least recently used page
        PageID evictedPageId = INVALID_VALUE;
        if(lruList.size() != 0){
            evictedPageId = lruList.back();
            map.erase(evictedPageId);
            lruList.pop_back();
        }
        return evictedPageId;
    }

};

constexpr size_t MAX_PAGES_IN_MEMORY = 10;

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, SlottedPage>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;

public:
    BufferManager(bool storage_manager_truncate_mode = true): 
        storage_manager(storage_manager_truncate_mode),
        policy(std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY)) {
            storage_manager.extend(MAX_PAGES);
    }
    
    ~BufferManager() {
        for (auto& pair : pageMap) {
            flushPage(pair.first);
        }
    }

    SlottedPage& fix_page(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            auto evictedPageId = policy->evict();
            if(evictedPageId != INVALID_VALUE){
                // std::cout << "Evicting page " << evictedPageId << "\n";
                storage_manager.flush(evictedPageId, 
                                      pageMap[evictedPageId]);
            }
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        // std::cout << "Loading page: " << page_id << "\n";
        pageMap[page_id] = std::move(*page);
        return pageMap[page_id];
    }

    void flushPage(int page_id) {
        storage_manager.flush(page_id, pageMap[page_id]);
    }

    void extend(){
        storage_manager.extend();
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

};

struct pair_hash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::pair<T1, T2>& pair) const {
        auto h1 = std::hash<T1>{}(pair.first);
        auto h2 = std::hash<T2>{}(pair.second);
        return h1 ^ (h2 << 1); // Combine hashes
    }
};

class PropertyValue {
public:
    std::variant<int, float, std::string> value;
    FieldType type;

    PropertyValue() : value(0), type(INT) {}

    PropertyValue(int intValue) : value(intValue), type(INT) {}
    PropertyValue(float floatValue) : value(floatValue), type(FLOAT) {}
    PropertyValue(const std::string& stringValue) : value(stringValue), type(STRING) {}

    void print() const {
        switch (type) {
            case INT:
                std::cout << std::get<int>(value);
                break;
            case FLOAT:
                std::cout << std::get<float>(value);
                break;
            case STRING:
                std::cout << std::get<std::string>(value);
                break;
        }
    }

    // Equality operator
    bool operator==(const PropertyValue& other) const {
        if (type != other.type) {
            return false;
        }
        switch (type) {
            case INT:
                return std::get<int>(value) == std::get<int>(other.value);
            case FLOAT:
                return std::get<float>(value) == std::get<float>(other.value);
            case STRING:
                return std::get<std::string>(value) == std::get<std::string>(other.value);
        }
        return false;
    }

    // Less-than operator for comparisons
    bool operator<(const PropertyValue& other) const {
        if (type != other.type) {
            return type < other.type; // Compare types if they differ
        }
        switch (type) {
            case INT:
                return std::get<int>(value) < std::get<int>(other.value);
            case FLOAT:
                return std::get<float>(value) < std::get<float>(other.value);
            case STRING:
                return std::get<std::string>(value) < std::get<std::string>(other.value);
        }
        return false;
    }

    // Getter for integer value
    int asInt() const {
        if (type != INT) {
            throw std::bad_variant_access();
        }
        return std::get<int>(value);
    }

    // Getter for float value
    float asFloat() const {
        if (type != FLOAT) {
            throw std::bad_variant_access();
        }
        return std::get<float>(value);
    }

    // Getter for string value
    std::string asString() const {
        if (type != STRING) {
            throw std::bad_variant_access();
        }
        return std::get<std::string>(value);
    }
};

// Class representing a node in the graph
constexpr size_t MaxPropertyCount = 10;

class Node {
public:
    uint32_t id; // Node ID
    std::unordered_map<std::string, PropertyValue> properties; // Properties
    size_t property_count = 0; // Current number of properties

    // Constructor
    Node(uint32_t id) : id(id) {}

    // Add a property to the node
    void addProperty(const std::string& name, const PropertyValue& value) {
        if (property_count >= MaxPropertyCount) {
            throw std::overflow_error("Maximum property count reached");
        }
        properties[name] = value;
        property_count++;
    }

    // Get a property value by name
    std::optional<PropertyValue> getProperty(const std::string& name) const {
        auto it = properties.find(name);
        if (it != properties.end()) {
            return it->second;
        }
        return std::nullopt; // Property not found
    }

    bool removeProperty(const std::string& name) {
        auto it = properties.find(name);
        if (it != properties.end()) {
            properties.erase(it);
            property_count--;
            return true;
        }
        return false;
    }

    // Print the node's details
    void print() const {
        std::cout << "Node " << id << " [ ";
        for (const auto& [key, value] : properties) {
            std::cout << key << ": ";
            value.print();
            std::cout << ", ";
        }
        std::cout << "]\n";
    }
};

class SNode {
public:
    uint32_t id; // Node ID
    std::array<std::string, MaxPropertyCount> property_names; // Property names (keys)
    std::array<PropertyValue, MaxPropertyCount> property_values; // Property values
    size_t property_count = 0; // Current number of properties

    // Constructor
    SNode(uint32_t id) : id(id) {}

    // Add a property to the node
    void addProperty(const std::string& name, const PropertyValue& value) {
        if (property_count >= MaxPropertyCount) {
            throw std::overflow_error("Maximum property count reached");
        }
        property_names[property_count] = name;
        property_values[property_count] = value;
        property_count++;
    }

    // Print the node's details
    Node convert() const {
        Node node(id); // Create a Node object with the same ID
        for (size_t i = 0; i < property_count; ++i) {
            node.addProperty(property_names[i], PropertyValue(property_values[i]));
        }
        return node;
    }

    void print() const {
        std::cout << "Node " << id << " [ ";
        for (size_t i = 0; i < property_count; ++i) {
            std::cout << property_names[i] << ": ";
            property_values[i].print();
            std::cout << ", ";
        }
        std::cout << "]\n";
    }
};

// Class representing an edge in the graph
class Edge {
public:
    uint32_t id;
    uint32_t source;
    uint32_t target;
    std::unordered_map<std::string, PropertyValue> properties;
    size_t property_count = 0;

    Edge(uint32_t id, uint32_t source, uint32_t target) : id(id), source(source), target(target) {}

    void addProperty(const std::string& name, const PropertyValue& value) {
        if (property_count >= MaxPropertyCount) {
            throw std::overflow_error("Maximum property count reached");
        }
        properties[name] = value;
        property_count++;
    }

    std::optional<PropertyValue> getProperty(const std::string& name) const {
        auto it = properties.find(name);
        if (it != properties.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool removeProperty(const std::string& name) {
        auto it = properties.find(name);
        if (it != properties.end()) {
            properties.erase(it);
            property_count--;
            return true;
        }
        return false;
    }

    void print() const {
        std::cout << "Edge " << id << " (" << source << " -> " << target << ") [ ";
        for (const auto& [key, value] : properties) {
            std::cout << key << ": ";
            value.print();
            std::cout << ", ";
        }
        std::cout << "]\n";
    }
};

class SEdge {
public:
    uint32_t id;
    uint32_t source;
    uint32_t target;
    std::array<std::string, MaxPropertyCount> property_names;
    std::array<PropertyValue, MaxPropertyCount> property_values;
    size_t property_count = 0;

    SEdge(uint32_t id, uint32_t source, uint32_t target) : id(id), source(source), target(target) {}

    void addProperty(const std::string& name, const PropertyValue& value) {
        if (property_count >= MaxPropertyCount) {
            throw std::overflow_error("Maximum property count reached");
        }
        property_names[property_count] = name;
        property_values[property_count] = value;
        property_count++;
    }

    Edge convert() const {
        Edge edge(id, source, target);
        for (size_t i = 0; i < property_count; ++i) {
            edge.addProperty(property_names[i], property_values[i]);
        }
        return edge;
    }

    void print() const {
        std::cout << "Edge " << id << " (" << source << " -> " << target << ") [ ";
        for (size_t i = 0; i < property_count; ++i) {
            std::cout << property_names[i] << ": ";
            property_values[i].print();
            std::cout << ", ";
        }
        std::cout << "]\n";
    }
};

enum class GraphType {
    DIRECTED,
    UNDIRECTED
};

static constexpr size_t MAX_NODES = 180;

class GraphManager {
private:
    BufferManager& buffer_manager;


    uint32_t next_node_id = 1;
    uint32_t next_edge_id = MAX_NODES + 1;
    std::optional<uint32_t> adj_matrix[MAX_NODES][MAX_NODES] = {}; // Fixed-size adjacency matrix using bool

public:
    GraphManager(BufferManager& bm) : buffer_manager(bm) {
    }

    SNode* createNode(const std::unordered_map<std::string, PropertyValue>& properties) {
        if (next_node_id > MAX_NODES) {
            throw std::overflow_error("Maximum number of nodes exceeded");
        }
        uint32_t id = next_node_id++;
        SlottedPage* page = &buffer_manager.fix_page(id);
        SNode* node = new (page->page_data.get()) SNode(id);

        for (const auto& [key, value] : properties) {
            node->addProperty(key, value);
        }

        buffer_manager.flushPage(id);
        return node;
    }

    bool addNodeProperty(uint32_t node_id, const std::string& property_name, const PropertyValue& value) {
        if (node_id >= MAX_NODES) {
            std::cerr << "Node ID exceeds maximum allowed value.\n";
            return false;
        }

        SlottedPage* page = &buffer_manager.fix_page(node_id);
        SNode* node = reinterpret_cast<SNode*>(page->page_data.get());

        node->addProperty(property_name, value);
        buffer_manager.flushPage(node_id);
        return true;
    }

    SEdge* createEdge(uint32_t source, uint32_t target, const std::unordered_map<std::string, PropertyValue>& properties, bool is_directed = true) {
        if (source > MAX_NODES || target > MAX_NODES) {
            throw std::out_of_range("Source or target node ID exceeds maximum node limit");
        }

        uint32_t id = next_edge_id++;
        SlottedPage* page = &buffer_manager.fix_page(id);
        SEdge* sedge = new (page->page_data.get()) SEdge(id, source, target);

        for (const auto& [key, value] : properties) {
            sedge->addProperty(key, value);
        }

        Edge edge = sedge->convert();
        adj_matrix[source - 1][target - 1] = sedge->id;
        if (!is_directed) {
            adj_matrix[target - 1][source - 1] = sedge->id;
        }

        buffer_manager.flushPage(id);
        return sedge;
    }

    bool addEdgeProperty(uint32_t edge_id, const std::string& property_name, const PropertyValue& value) {
        SlottedPage* page = &buffer_manager.fix_page(edge_id);
        SEdge* edge = reinterpret_cast<SEdge*>(page->page_data.get());

        edge->addProperty(property_name, value);
        buffer_manager.flushPage(edge_id);
        return true;
    }

    std::vector<size_t> findNthDegreeConnections(size_t start_node, size_t degree) {
        if (start_node < 1 || start_node > MAX_NODES) {
            throw std::out_of_range("Start node is out of range");
        }

        if (degree == 0) {
            throw std::invalid_argument("Degree must be greater than 0");
        }

        std::vector<bool> visited(MAX_NODES, false);
        std::queue<std::pair<size_t, size_t>> q;
        std::vector<size_t> nth_degree_connections;

        q.push({start_node - 1, 0});
        visited[start_node - 1] = true;

        while (!q.empty()) {
            auto [current_node, current_degree] = q.front();
            q.pop();

            if (current_degree == degree) {
                SlottedPage* page = &buffer_manager.fix_page(current_node + 1);
                SNode* snode = reinterpret_cast<SNode*>(page->page_data.get());
                Node node = snode->convert();
                if (node.getProperty("type").has_value() && node.getProperty("type").value() == PropertyValue("user")) { 
                    nth_degree_connections.push_back(current_node + 1);
                }
                continue;
            }

            for (size_t neighbor = 0; neighbor < MAX_NODES; ++neighbor) {
                if (adj_matrix[current_node][neighbor] && !visited[neighbor]) {
                    q.push({neighbor, current_degree + 1});
                    visited[neighbor] = true;
                }
            }
        }

        return nth_degree_connections;
    }

    std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> findConnectionsAndLikes(uint32_t user_id) {
        if (user_id < 1 || user_id > MAX_NODES) {
            throw std::out_of_range("User ID is out of range");
        }

        std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> result = {
            {"colleagues", {}},
            {"friends", {}}
        };

        // Traverse the adjacency matrix
        for (size_t neighbor = 0; neighbor < MAX_NODES; ++neighbor) {
            if (!adj_matrix[user_id - 1][neighbor].has_value()) {
                continue; // Skip if there is no connection
            }


            // Retrieve neighbor node details
            SlottedPage* neighbor_page = &buffer_manager.fix_page(neighbor + 1);
            SNode* snode = reinterpret_cast<SNode*>(neighbor_page->page_data.get());
            Node neighbor_node = snode->convert();

            auto type_property = neighbor_node.getProperty("type");
            if (!type_property.has_value() || type_property.value().asString() != "user") {
                continue;
            }

            auto name_property = neighbor_node.getProperty("name");
            if (!name_property.has_value()) {
                continue;
            }

            // Retrieve the edge details
            uint32_t edge_id = adj_matrix[user_id - 1][neighbor].value();
            SlottedPage* edge_page = &buffer_manager.fix_page(edge_id);
            SEdge* sedge = reinterpret_cast<SEdge*>(edge_page->page_data.get());
            Edge edge = sedge->convert();

            auto relationship_property = edge.getProperty("relationship");
            if (!relationship_property.has_value()) {
                continue;
            }

            std::string relationship = relationship_property.value().asString();

            // Count likes on posts connected to this neighbor
            uint32_t likes = 0;
            for (size_t neighbor2 = 0; neighbor2 < MAX_NODES; ++neighbor2) {
                if (!adj_matrix[neighbor][neighbor2].has_value()) {
                    continue;
                }

                // Check if the connected node is a post
                SlottedPage* neighbor2_page = &buffer_manager.fix_page(neighbor2 + 1);
                SNode* snode2 = reinterpret_cast<SNode*>(neighbor2_page->page_data.get());
                Node post_node = snode2->convert();

                auto post_type_property = post_node.getProperty("type");
                if (!post_type_property.has_value() || post_type_property.value().asString() != "post") {
                    continue;
                }

                auto post_likes_property = post_node.getProperty("likes");
                if (post_likes_property.has_value()) {
                    likes += post_likes_property.value().asInt();
                }
            }

            // Add to the result based on the relationship type
            std::string name = name_property.value().asString();
            if (relationship == "colleagues") {
                result["colleagues"].push_back({name, likes});
            } else if (relationship == "friends") {
                result["friends"].push_back({name, likes});
            }
        }

        return result;
    }

    void printNodes() const {
        std::unordered_set<size_t> nodes; // Use a set to ensure unique nodes
        std::cout << "Nodes in the graph:\n";

        for (size_t i = 0; i < MAX_NODES; ++i) {
            for (size_t j = 0; j < MAX_NODES; ++j) {
                if (adj_matrix[i][j]) {
                    nodes.insert(i + 1); // Add source node
                    nodes.insert(j + 1); // Add target node
                }
            }
        }

        // Print all unique nodes with their details
        for (size_t node_id : nodes) {
            SlottedPage* page = &buffer_manager.fix_page(node_id);
            SNode* node = reinterpret_cast<SNode*>(page->page_data.get());
            node->print(); // Use the node's print function
        }
    }

    void printEdges() const {
        std::cout << "Edges in the graph:\n";
        std::unordered_set<std::pair<size_t, size_t>, pair_hash> processed_edges;

        for (size_t i = 0; i < MAX_NODES; ++i) {
            for (size_t j = 0; j < MAX_NODES; ++j) {
                if (adj_matrix[i][j]) {
                    // Ensure (i + 1, j + 1) is processed only once
                    auto edge = (i < j) ? std::make_pair(i, j) : std::make_pair(j, i);
                    if (processed_edges.find(edge) == processed_edges.end()) {
                        std::cout << "Edge: " << (i + 1) << " -> " << (j + 1);
                        if (adj_matrix[j][i]) {
                            std::cout << " (Undirected)";
                        }
                        std::cout << "\n";
                        processed_edges.insert(edge);
                    }
                }
            }
        }
    }
};

std::unordered_map<std::string, uint32_t> populateGraph(GraphManager& graph_manager) {
    std::unordered_map<uint32_t, uint32_t> user_id_to_node_id;
    std::unordered_map<std::string, uint32_t> name_to_node_id;

    // Step 1: Read `users.csv` and create nodes
    std::ifstream users_file("users.csv");

    std::string line;
    getline(users_file, line); // Skip header

    while (getline(users_file, line)) {
        std::istringstream iss(line);
        std::string user_id, name, age, location;

        getline(iss, user_id, ',');
        getline(iss, name, ',');
        getline(iss, age, ',');
        getline(iss, location, ',');

        SNode* snode = graph_manager.createNode({
            {"type", PropertyValue("user")},
            {"user_id", PropertyValue(std::stoi(user_id))},
            {"name", PropertyValue(name)},
            {"age", PropertyValue(std::stoi(age))},
            {"location", PropertyValue(location)}
        });

        user_id_to_node_id[std::stoi(user_id)] = snode->id;
        name_to_node_id[name] = snode->id;
    }

    users_file.close();

    // Step 2: Read `connections.csv` and create edges
    std::ifstream connections_file("connections.csv");

    getline(connections_file, line); // Skip header

    while (getline(connections_file, line)) {
        std::istringstream iss(line);
        std::string source, target, relationship;

        getline(iss, source, ',');
        getline(iss, target, ',');
        getline(iss, relationship, ',');

        graph_manager.createEdge(user_id_to_node_id[std::stoi(source)], user_id_to_node_id[std::stoi(target)], {
            {"relationship", PropertyValue(relationship)}, 
        }, false);
    }

    connections_file.close();

    // Step 3: Read `posts.csv` and create nodes and edges
    std::ifstream posts_file("posts.csv");

    getline(posts_file, line); // Skip header

    while (getline(posts_file, line)) {
        std::istringstream iss(line);
        std::string user_id, post_id, content, likes;

        getline(iss, user_id, ',');
        getline(iss, post_id, ',');
        getline(iss, content, ',');
        getline(iss, likes, ',');

        SNode* snode = graph_manager.createNode({
            {"type", PropertyValue("post")},
            {"post_id", PropertyValue(std::stoi(post_id))},
            {"content", PropertyValue(content)},
            {"likes", PropertyValue(std::stoi(likes))},
        });

        graph_manager.createEdge(user_id_to_node_id[std::stoi(user_id)], snode->id, {
            {"label", PropertyValue("posted")},
        });
    }

    posts_file.close();
    return name_to_node_id;
}

void test_createNode() {
    BufferManager buffer_manager;
    GraphManager graph_manager(buffer_manager);

    auto node1 = graph_manager.createNode({{"name", PropertyValue("Node1")}, {"type", PropertyValue("user")}});
    assert(node1->id == 1);

    auto node2 = graph_manager.createNode({{"name", PropertyValue("Node2")}, {"type", PropertyValue("user")}});
    assert(node2->id == 2);

    SlottedPage* page1 = &buffer_manager.fix_page(node1->id);
    SNode* snode1 = reinterpret_cast<SNode*>(page1->page_data.get());
    assert(snode1->id == 1);

    SlottedPage* page2 = &buffer_manager.fix_page(node2->id);
    SNode* snode2 = reinterpret_cast<SNode*>(page2->page_data.get());
    assert(snode2->id == 2);

    std::cout << "\033[1m\033[32mPassed: test_createNode\033[0m" << std::endl;
}

void test_addNodeProperty() {
    BufferManager buffer_manager;
    GraphManager graph_manager(buffer_manager);

    auto node = graph_manager.createNode({{"name", PropertyValue("Node1")}, {"type", PropertyValue("user")}});
    assert(graph_manager.addNodeProperty(node->id, "age", PropertyValue(25)) == true);

    SlottedPage* page = &buffer_manager.fix_page(node->id);
    SNode* snode = reinterpret_cast<SNode*>(page->page_data.get());
    Node converted_node = snode->convert();

    assert(converted_node.getProperty("age").has_value());
    assert(converted_node.getProperty("age").value() == PropertyValue(25));

    std::cout << "\033[1m\033[32mPassed: test_addNodeProperty\033[0m" << std::endl;
}

void test_createEdge() {
    BufferManager buffer_manager;
    GraphManager graph_manager(buffer_manager);

    auto node1 = graph_manager.createNode({{"name", PropertyValue("Node1")}, {"type", PropertyValue("user")}});
    auto node2 = graph_manager.createNode({{"name", PropertyValue("Node2")}, {"type", PropertyValue("user")}});

    auto edge = graph_manager.createEdge(node1->id, node2->id, {{"weight", PropertyValue(10)}}, true);
    assert(edge->id == MAX_NODES + 1);

    SlottedPage* page = &buffer_manager.fix_page(edge->id);
    SEdge* sedge = reinterpret_cast<SEdge*>(page->page_data.get());
    assert(sedge->source == node1->id);
    assert(sedge->target == node2->id);

    std::cout << "\033[1m\033[32mPassed: test_createEdge\033[0m" << std::endl;
}

void test_addEdgeProperty() {
    BufferManager buffer_manager;
    GraphManager graph_manager(buffer_manager);

    auto node1 = graph_manager.createNode({{"name", PropertyValue("Node1")}, {"type", PropertyValue("user")}});
    auto node2 = graph_manager.createNode({{"name", PropertyValue("Node2")}, {"type", PropertyValue("user")}});
    auto edge = graph_manager.createEdge(node1->id, node2->id, {{"weight", PropertyValue(10)}}, true);

    assert(graph_manager.addEdgeProperty(edge->id, "label", PropertyValue("friendship")) == true);

    SlottedPage* page = &buffer_manager.fix_page(edge->id);
    SEdge* sedge = reinterpret_cast<SEdge*>(page->page_data.get());
    Edge converted_edge = sedge->convert();
    assert(converted_edge.getProperty("label").has_value());
    assert(converted_edge.getProperty("label").value() == PropertyValue("friendship"));

    std::cout << "\033[1m\033[32mPassed: test_addEdgeProperty\033[0m" << std::endl;
}

int main() {
    try {
        while (true) {
            std::cout << PROMPT_COLOR << "Choose an option:\n";
            std::cout << "1. Run all unit tests\n";
            std::cout << "2. Find nth-degree connections\n";
            std::cout << "   (Find all nodes within a specified degree of connection to a given person)\n";
            std::cout << "3. Find connections and likes\n";
            std::cout << "   (Find the number of colleagues and friends a user has, along with their likes)\n";
            std::cout << "4. Exit\n";
            std::cout << "Enter your choice: " << RESET;

            int choice;
            std::cin >> choice;

            if (choice == 4) {
                std::cout << RESULT_COLOR << "Thanks for trying out this Graph Database extension. Goodbye!" << RESET << "\n";
                break;
            }

            switch (choice) {
                case 1: {
                    std::cout << RESULT_COLOR << "Running unit tests...\n" << RESET;
                    test_createNode();
                    test_addNodeProperty();
                    test_createEdge();
                    test_addEdgeProperty();
                    break;
                }
                case 2: {
                    BufferManager buffer_manager;
                    GraphManager graph_manager(buffer_manager);
                    std::cout << PROMPT_COLOR << "Populating graph database...\n" << RESET;
                    auto name_to_node_id = populateGraph(graph_manager);

                    std::string name;
                    size_t degree;
                    std::cout << PROMPT_COLOR << "Enter the name of the person: " << RESET;
                    std::cin.ignore();
                    std::getline(std::cin, name);
                    std::cout << PROMPT_COLOR << "Enter the degree of connections to find: " << RESET;
                    std::cin >> degree;

                    if (name_to_node_id.find(name) == name_to_node_id.end()) {
                        std::cerr << RESULT_COLOR << "Error: Name not found in the database.\n" << RESET;
                        break;
                    }

                    auto start_time = std::chrono::high_resolution_clock::now();
                    std::vector<size_t> nth_degree_connections = graph_manager.findNthDegreeConnections(name_to_node_id[name], degree);
                    auto end_time = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<double> duration = end_time - start_time;
                    std::cout << RESULT_COLOR << "Execution time for finding " << degree << "-degree connections: "
                              << duration.count() << " seconds\n" << RESET;

                    std::cout << RESULT_COLOR << "Nodes connected to " << name << " within " << degree << " degrees:\n" << RESET;
                    for (size_t node_id : nth_degree_connections) {
                        SlottedPage* page = &buffer_manager.fix_page(node_id);
                        SNode* node = reinterpret_cast<SNode*>(page->page_data.get());
                        std::cout << RESULT_COLOR;
                        node->print();
                        std::cout << RESET;
                    }
                    break;
                }
                case 3: {
                    BufferManager buffer_manager;
                    GraphManager graph_manager(buffer_manager);
                    std::cout << PROMPT_COLOR << "Populating graph database...\n" << RESET;
                    auto name_to_node_id = populateGraph(graph_manager);

                    std::string name;
                    std::cout << PROMPT_COLOR << "Enter the name of the person: " << RESET;
                    std::cin.ignore();
                    std::getline(std::cin, name);

                    if (name_to_node_id.find(name) == name_to_node_id.end()) {
                        std::cerr << RESULT_COLOR << "Error: Name not found in the database.\n" << RESET;
                        break;
                    }

                    std::cout << PROMPT_COLOR << "Finding connections and likes for " << name << "...\n" << RESET;
                    auto start_time = std::chrono::high_resolution_clock::now();
                    auto connections = graph_manager.findConnectionsAndLikes(name_to_node_id[name]);
                    auto end_time = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<double> duration = end_time - start_time;
                    std::cout << RESULT_COLOR << "Execution time for finding connections and likes: "
                              << duration.count() << " seconds\n" << RESET;

                    std::cout << RESULT_COLOR << name << "'s Colleagues:\n" << RESET;
                    for (const auto& [colleague_name, likes] : connections["colleagues"]) {
                        std::cout << RESULT_COLOR << "Name: " << colleague_name << ", Likes: " << likes << "\n" << RESET;
                    }

                    std::cout << RESULT_COLOR << name << "'s Friends:\n" << RESET;
                    for (const auto& [friend_name, likes] : connections["friends"]) {
                        std::cout << RESULT_COLOR << "Name: " << friend_name << ", Likes: " << likes << "\n" << RESET;
                    }
                    break;
                }
                default: {
                    std::cerr << RESULT_COLOR << "Error: Invalid choice. Please enter a number between 1 and 4.\n" << RESET;
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << RESULT_COLOR << "An error occurred: " << e.what() << "\n" << RESET;
    }

    return 0;
}